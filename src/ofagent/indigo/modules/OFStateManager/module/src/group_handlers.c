/****************************************************************
 *
 *        Copyright 2013, Big Switch Networks, Inc. 
 * 
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 * 
 *        http://www.eclipse.org/legal/epl-v10.html
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 ****************************************************************/

/**
 * @file
 * @brief OpenFlow message handlers for group messages
 *
 * See detailed documentation in the Indigo architecture headers.
 */

#include "ofstatemanager_log.h"

#include <OFStateManager/ofstatemanager_config.h>
#include <OFConnectionManager/ofconnectionmanager.h>
#include <indigo/indigo.h>
#include <indigo/of_state_manager.h>
#include <indigo/forwarding.h>
#include <loci/loci.h>
#include <AIM/aim_list.h>
#include "ofstatemanager_decs.h"
#include "ofstatemanager_int.h"
#include "handlers.h"

typedef struct ind_core_group_s {
    list_links_t links;
    uint32_t id;
    uint32_t type;
    of_list_bucket_t *buckets;
    indigo_time_t creation_time;
} ind_core_group_t;

static LIST_DEFINE(ind_core_groups_list);

static ind_core_group_t *
ind_core_group_lookup(uint32_t id)
{
    list_links_t *cur;
    LIST_FOREACH(&ind_core_groups_list, cur) {
        ind_core_group_t *group = container_of(cur, links, ind_core_group_t);
        if (group->id == id) {
            return group;
        }
    }
    return NULL;
}

#ifdef OFDPA_FIXUP
static indigo_error_t
ind_core_group_delete_one(ind_core_group_t *group)
{
    indigo_error_t result;
    result = indigo_fwd_group_delete(group->id);
    if (result >= 0) {
      of_object_delete(group->buckets);
      list_remove(&group->links);
      INDIGO_MEM_FREE(group);
    }
    return result;
}
#else
static void
ind_core_group_delete_one(ind_core_group_t *group)
{
    indigo_fwd_group_delete(group->id);
    of_object_delete(group->buckets);
    list_remove(&group->links);
    INDIGO_MEM_FREE(group);
}
#endif

indigo_error_t
ind_core_group_mod_handler(of_object_t *_obj, indigo_cxn_id_t cxn_id)
{
    of_group_mod_t *obj = _obj;
    uint32_t xid;
    uint16_t command;
    uint8_t type;
    uint32_t id;
    of_list_bucket_t buckets;
    ind_core_group_t *group = NULL;
    uint16_t err_type = OF_ERROR_TYPE_GROUP_MOD_FAILED;
    uint16_t err_code = OF_GROUP_MOD_FAILED_EPERM;
    indigo_error_t result;
#ifdef OFDPA_FIXUP
    uint32_t at_least_1grp_del;
#endif

    of_group_mod_xid_get(obj, &xid);
    of_group_mod_command_get(obj, &command);
    of_group_mod_group_type_get(obj, &type);
    of_group_mod_group_id_get(obj, &id);
    of_group_mod_buckets_bind(obj, &buckets);

    if (id <= OF_GROUP_MAX) {
        group = ind_core_group_lookup(id);
    }

    if (command == OF_GROUP_ADD) {
        if (group != NULL) {
            err_code = OF_GROUP_MOD_FAILED_GROUP_EXISTS;
            goto error;
        } else if (id > OF_GROUP_MAX) {
            err_code = OF_GROUP_MOD_FAILED_INVALID_GROUP;
            goto error;
        }

        result = indigo_fwd_group_add(id, type, &buckets);
        if (result < 0) {
            err_code = OF_GROUP_MOD_FAILED_INVALID_GROUP;
            goto error;
        }

        group = INDIGO_MEM_ALLOC(sizeof(*group));
        AIM_TRUE_OR_DIE(group != NULL);
        group->id = id;
        group->type = type;
        group->buckets = of_object_dup(&buckets);
        AIM_TRUE_OR_DIE(group->buckets != NULL);
        group->creation_time = INDIGO_CURRENT_TIME;

        list_push(&ind_core_groups_list, &group->links);
    } else if (command == OF_GROUP_MODIFY) {
        if (group == NULL) {
            err_code = OF_GROUP_MOD_FAILED_UNKNOWN_GROUP;
            goto error;
        }

        if (group->type == type) {
            result = indigo_fwd_group_modify(id, &buckets);
#ifdef OFDPA_FIXUP
            if (result < 0) {
                err_code = OF_GROUP_MOD_FAILED_INVALID_GROUP;
                ind_core_group_delete_one(group);
                goto error;
            }
#endif
        } else {
#ifdef OFDPA_FIXUP
          result = indigo_fwd_group_delete(id);
            if (result < 0) {
                err_code = OF_GROUP_MOD_FAILED_INVALID_GROUP;
                goto error;
            }
#else
              indigo_fwd_group_delete(id);
#endif
            result = indigo_fwd_group_add(id, type, &buckets);
        }

        if (result < 0) {
            err_code = OF_GROUP_MOD_FAILED_INVALID_GROUP;
            goto error;
        }

        group->type = type;
        of_object_delete(group->buckets);
        group->buckets = of_object_dup(&buckets);
        AIM_TRUE_OR_DIE(group->buckets != NULL);
    } else if (command == OF_GROUP_DELETE) {
        if (id == OF_GROUP_ALL) {
            list_links_t *cur, *next;
#ifdef OFDPA_FIXUP
            /* Go thru all groups with refcount 0 and delete them.
             * Do as many iterations as needed */
            do {
                at_least_1grp_del = 0;

                LIST_FOREACH_SAFE(&ind_core_groups_list, cur, next) {
                    group = container_of(cur, links, ind_core_group_t);
                    result = ind_core_group_delete_one(group);
                    if (result == 0) {
                        at_least_1grp_del = 1;
                    }
                }
            } while (at_least_1grp_del == 1);

            /* Now when all groups with refcount 0 are deleted check if
             * there are still groups available */
            LIST_FOREACH_SAFE(&ind_core_groups_list, cur, next) {
                group = container_of(cur, links, ind_core_group_t);
                result = ind_core_group_delete_one(group);
                if (result < 0) {
                    err_code = OF_GROUP_MOD_FAILED_INVALID_GROUP;
                    goto error;
                }
            }
#else
            LIST_FOREACH_SAFE(&ind_core_groups_list, cur, next) {
                group = container_of(cur, links, ind_core_group_t);
                ind_core_group_delete_one(group);
            }
#endif
        } else if (group != NULL) {
#ifdef OFDPA_FIXUP
            result = ind_core_group_delete_one(group);
            if (result < 0) {
                err_code = OF_GROUP_MOD_FAILED_INVALID_GROUP;
                goto error;
            }
#else
            ind_core_group_delete_one(group);
#endif
        } else if (id > OF_GROUP_MAX) {
            err_code = OF_GROUP_MOD_FAILED_INVALID_GROUP;
            goto error;
        }
    }

    of_object_delete(obj);
    return INDIGO_ERROR_NONE;

error:
    indigo_cxn_send_error_msg(obj->version, cxn_id, xid,
                              err_type, err_code, NULL);
    of_object_delete(obj);
    return INDIGO_ERROR_UNKNOWN;
}

static void
ind_core_group_stats_entry_populate(of_group_stats_entry_t *entry,
                                    ind_core_group_t *group,
                                    indigo_time_t current_time)
{
    uint32_t duration_sec, duration_nsec;

    of_group_stats_entry_group_id_set(entry, group->id);

    calc_duration(current_time, group->creation_time, &duration_sec, &duration_nsec);
    of_group_stats_entry_duration_sec_set(entry, duration_sec);
    of_group_stats_entry_duration_nsec_set(entry, duration_nsec);

    indigo_fwd_group_stats_get(group->id, entry);
}

/* TODO segment long replies */
indigo_error_t
ind_core_group_stats_request_handler(of_object_t *_obj,
                                     indigo_cxn_id_t cxn_id)
{
    of_group_stats_request_t *obj = _obj;
    of_group_stats_reply_t *reply;
    of_list_group_stats_entry_t entries;
    of_group_stats_entry_t *entry;
    uint32_t xid;
    uint32_t id;
    list_links_t *cur, *next;
    indigo_time_t current_time = INDIGO_CURRENT_TIME;

    of_group_stats_request_group_id_get(obj, &id);

    reply = of_group_stats_reply_new(obj->version);
    AIM_TRUE_OR_DIE(reply != NULL);

    of_group_stats_request_xid_get(obj, &xid);
    of_group_stats_reply_xid_set(reply, xid);
    of_group_stats_reply_entries_bind(reply, &entries);

    entry = of_group_stats_entry_new(entries.version);
    AIM_TRUE_OR_DIE(entry != NULL);

    if (id == OF_GROUP_ALL) {
        LIST_FOREACH_SAFE(&ind_core_groups_list, cur, next) {
            ind_core_group_t *group = container_of(cur, links, ind_core_group_t);
            ind_core_group_stats_entry_populate(entry, group, current_time);

            if (of_list_append(&entries, entry) < 0) {
                break;
            }

            /* HACK unable to truncate existing object */
            of_object_delete(entry);
            entry = of_group_stats_entry_new(entries.version);
            AIM_TRUE_OR_DIE(entry != NULL);
        }
    } else if (id <= OF_GROUP_MAX) {
        ind_core_group_t *group = ind_core_group_lookup(id);
        if (group != NULL) {
            ind_core_group_stats_entry_populate(entry, group, current_time);

            if (of_list_append(&entries, entry) < 0) {
                AIM_DIE("unexpected failure appending single group stats entry");
            }
        }
    }

    of_object_delete(entry);
    of_object_delete(obj);

    IND_CORE_MSG_SEND(cxn_id, reply);
    return INDIGO_ERROR_NONE;
}

/* TODO segment long replies */
indigo_error_t
ind_core_group_desc_stats_request_handler(of_object_t *_obj,
                                          indigo_cxn_id_t cxn_id)
{
    of_group_desc_stats_request_t *obj = _obj;
    of_group_desc_stats_reply_t *reply;
    of_list_group_desc_stats_entry_t entries;
    of_group_desc_stats_entry_t *entry;
    uint32_t xid;
    list_links_t *cur, *next;

    reply = of_group_desc_stats_reply_new(obj->version);
    AIM_TRUE_OR_DIE(reply != NULL);

    of_group_desc_stats_request_xid_get(obj, &xid);
    of_group_desc_stats_reply_xid_set(reply, xid);
    of_group_desc_stats_reply_entries_bind(reply, &entries);

    entry = of_group_desc_stats_entry_new(entries.version);
    AIM_TRUE_OR_DIE(entry != NULL);

    LIST_FOREACH_SAFE(&ind_core_groups_list, cur, next) {
        ind_core_group_t *group = container_of(cur, links, ind_core_group_t);
        of_group_desc_stats_entry_group_type_set(entry, group->type);
        of_group_desc_stats_entry_group_id_set(entry, group->id);
        if (of_group_desc_stats_entry_buckets_set(entry, group->buckets) < 0) {
            AIM_DIE("unexpected failure setting group desc stats entry buckets");
        }

        if (of_list_append(&entries, entry) < 0) {
            break;
        }
    }

    of_object_delete(entry);
    of_object_delete(obj);

    IND_CORE_MSG_SEND(cxn_id, reply);
    return INDIGO_ERROR_NONE;
}

indigo_error_t
ind_core_group_features_stats_request_handler(of_object_t *_obj,
                                              indigo_cxn_id_t cxn_id)
{
    of_group_features_stats_request_t *obj = _obj;
    ind_core_unhandled_message(obj, cxn_id);
    return INDIGO_ERROR_NONE;
}
