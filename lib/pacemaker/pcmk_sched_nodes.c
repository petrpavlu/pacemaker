/*
 * Copyright 2004-2022 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU General Public License version 2
 * or later (GPLv2+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>
#include <crm/msg_xml.h>
#include <crm/lrmd.h>       // lrmd_event_data_t
#include <crm/common/xml_internal.h>
#include <pacemaker-internal.h>
#include <pacemaker.h>
#include "libpacemaker_private.h"

/*!
 * \internal
 * \brief Check whether a node is available to run resources
 *
 * \param[in] node  Node to check
 *
 * \return true if node is online and not shutting down, unclean, or in standby
 *         or maintenance mode, otherwise false
 */
bool
pcmk__node_available(const pe_node_t *node)
{
    // @TODO Should we add (node->weight >= 0)?
    return (node != NULL) && (node->details != NULL) && node->details->online
            && !node->details->shutdown && !node->details->unclean
            && !node->details->standby && !node->details->maintenance;
}

/*!
 * \internal
 * \brief Copy a hash table of node objects
 *
 * \param[in] nodes  Hash table to copy
 *
 * \return New copy of nodes (or NULL if nodes is NULL)
 */
GHashTable *
pcmk__copy_node_table(GHashTable *nodes)
{
    GHashTable *new_table = NULL;
    GHashTableIter iter;
    pe_node_t *node = NULL;

    if (nodes == NULL) {
        return NULL;
    }
    new_table = pcmk__strkey_table(NULL, free);
    g_hash_table_iter_init(&iter, nodes);
    while (g_hash_table_iter_next(&iter, NULL, (gpointer *) &node)) {
        pe_node_t *new_node = pe__copy_node(node);

        g_hash_table_insert(new_table, (gpointer) new_node->details->id,
                            new_node);
    }
    return new_table;
}

/*!
 * \internal
 * \brief Copy a list of node objects
 *
 * \param[in] list   List to copy
 * \param[in] reset  Set copies' scores to 0
 *
 * \return New list of shallow copies of nodes in original list
 */
GList *
pcmk__copy_node_list(const GList *list, bool reset)
{
    GList *result = NULL;

    for (const GList *gIter = list; gIter != NULL; gIter = gIter->next) {
        pe_node_t *new_node = NULL;
        pe_node_t *this_node = (pe_node_t *) gIter->data;

        new_node = pe__copy_node(this_node);
        if (reset) {
            new_node->weight = 0;
        }
        result = g_list_prepend(result, new_node);
    }
    return result;
}

struct node_weight_s {
    pe_node_t *active;
    pe_working_set_t *data_set;
};

/*!
 * \internal
 * \brief Compare two nodes for allocation desirability
 *
 * Given two nodes, check which one is more preferred by allocation criteria
 * such as node weight and utilization.
 *
 * \param[in] a     First node to compare
 * \param[in] b     Second node to compare
 * \param[in] data  Sort data (as struct node_weight_s *)
 *
 * \return -1 if \p a is preferred, +1 if \p b is preferred, or 0 if they are
 *         equally preferred
 */
static gint
compare_nodes(gconstpointer a, gconstpointer b, gpointer data)
{
    const pe_node_t *node1 = (const pe_node_t *) a;
    const pe_node_t *node2 = (const pe_node_t *) b;
    struct node_weight_s *nw = data;

    int node1_weight = 0;
    int node2_weight = 0;

    int result = 0;

    if (a == NULL) {
        return 1;
    }
    if (b == NULL) {
        return -1;
    }

    // Compare node weights

    node1_weight = pcmk__node_available(node1)? node1->weight : -INFINITY;
    node2_weight = pcmk__node_available(node2)? node2->weight : -INFINITY;

    if (node1_weight > node2_weight) {
        crm_trace("%s (%d) > %s (%d) : weight",
                  node1->details->uname, node1_weight, node2->details->uname,
                  node2_weight);
        return -1;
    }

    if (node1_weight < node2_weight) {
        crm_trace("%s (%d) < %s (%d) : weight",
                  node1->details->uname, node1_weight, node2->details->uname,
                  node2_weight);
        return 1;
    }

    crm_trace("%s (%d) == %s (%d) : weight",
              node1->details->uname, node1_weight, node2->details->uname,
              node2_weight);

    // If appropriate, compare node utilization

    if (pcmk__str_eq(nw->data_set->placement_strategy, "minimal",
                     pcmk__str_casei)) {
        goto equal;
    }

    if (pcmk__str_eq(nw->data_set->placement_strategy, "balanced",
                     pcmk__str_casei)) {
        result = pcmk__compare_node_capacities(node1, node2);
        if (result < 0) {
            crm_trace("%s > %s : capacity (%d)",
                      node1->details->uname, node2->details->uname, result);
            return -1;
        } else if (result > 0) {
            crm_trace("%s < %s : capacity (%d)",
                      node1->details->uname, node2->details->uname, result);
            return 1;
        }
    }

    // Compare number of allocated resources

    if (node1->details->num_resources < node2->details->num_resources) {
        crm_trace("%s (%d) > %s (%d) : resources",
                  node1->details->uname, node1->details->num_resources,
                  node2->details->uname, node2->details->num_resources);
        return -1;

    } else if (node1->details->num_resources > node2->details->num_resources) {
        crm_trace("%s (%d) < %s (%d) : resources",
                  node1->details->uname, node1->details->num_resources,
                  node2->details->uname, node2->details->num_resources);
        return 1;
    }

    // Check whether one node is already running desired resource

    if (nw->active != NULL) {
        if (nw->active->details == node1->details) {
            crm_trace("%s (%d) > %s (%d) : active",
                      node1->details->uname, node1->details->num_resources,
                      node2->details->uname, node2->details->num_resources);
            return -1;
        } else if (nw->active->details == node2->details) {
            crm_trace("%s (%d) < %s (%d) : active",
                      node1->details->uname, node1->details->num_resources,
                      node2->details->uname, node2->details->num_resources);
            return 1;
        }
    }

    // If all else is equal, prefer node with lowest-sorting name
equal:
    crm_trace("%s = %s", node1->details->uname, node2->details->uname);
    return strcmp(node1->details->uname, node2->details->uname);
}

/*!
 * \internal
 * \brief Sort a list of nodes by allocation desirability
 *
 * \param[in] nodes        Node list to sort
 * \param[in] active_node  If not NULL, node currently running resource
 * \param[in] data_set     Cluster working set
 *
 * \return New head of sorted list
 */
GList *
pcmk__sort_nodes(GList *nodes, pe_node_t *active_node,
                 pe_working_set_t *data_set)
{
    struct node_weight_s nw = { active_node, data_set };

    return g_list_sort_with_data(nodes, compare_nodes, &nw);
}

/*!
 * \internal
 * \brief Check whether any node is available to run resources
 *
 * \param[in] nodes  Nodes to check
 *
 * \return true if any node in \p nodes is available to run resources,
 *         otherwise false
 */
bool
pcmk__any_node_available(GHashTable *nodes)
{
    GHashTableIter iter;
    pe_node_t *node = NULL;

    if (nodes == NULL) {
        return false;
    }
    g_hash_table_iter_init(&iter, nodes);
    while (g_hash_table_iter_next(&iter, NULL, (void **) &node)) {
        if ((node->weight >= 0) && pcmk__node_available(node)) {
            return true;
        }
    }
    return false;
}

/*!
 * \internal
 * \brief Apply node health values for all nodes in cluster
 *
 * \param[in] data_set  Cluster working set
 */
void
pcmk__apply_node_health(pe_working_set_t *data_set)
{
    int base_health = 0;
    enum pcmk__health_strategy strategy;
    const char *strategy_str = pe_pref(data_set->config_hash,
                                       PCMK__OPT_NODE_HEALTH_STRATEGY);

    strategy = pcmk__parse_health_strategy(strategy_str);
    if (strategy == pcmk__health_strategy_none) {
        return;
    }
    crm_info("Applying node health strategy '%s'", strategy_str);

    // The progressive strategy can use a base health score
    if (strategy == pcmk__health_strategy_progressive) {
        base_health = pe__health_score(PCMK__OPT_NODE_HEALTH_BASE, data_set);
    }

    for (GList *iter = data_set->nodes; iter != NULL; iter = iter->next) {
        pe_node_t *node = (pe_node_t *) iter->data;
        int health = pe__sum_node_health_scores(node, base_health);

        // An overall health score of 0 has no effect
        if (health == 0) {
            continue;
        }
        crm_info("Node %s overall system health is %d",
                 node->details->uname, health);

        // Use node health as a location score for each resource on the node
        for (GList *r = data_set->resources; r != NULL; r = r->next) {
            pe_resource_t *rsc = (pe_resource_t *) r->data;

            bool constrain = true;

            if (health < 0) {
                /* Negative health scores do not apply to resources with
                 * allow-unhealthy-nodes=true.
                 */
                constrain = !crm_is_true(g_hash_table_lookup(rsc->meta,
                                         PCMK__META_ALLOW_UNHEALTHY_NODES));
            }
            if (constrain) {
                pcmk__new_location(strategy_str, rsc, health, NULL, node,
                                   data_set);
            } else {
                pe_rsc_trace(rsc, "%s is immune from health ban on %s",
                             rsc->id, node->details->uname);
            }
        }
    }
}
