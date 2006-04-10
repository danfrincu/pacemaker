/* $Id: group.c,v 1.60 2006/04/10 07:23:27 andrew Exp $ */
/* 
 * Copyright (C) 2004 Andrew Beekhof <andrew@beekhof.net>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <portability.h>

#include <pengine.h>
#include <pe_utils.h>
#include <crm/msg_xml.h>
#include <clplumbing/cl_misc.h>

extern gboolean rsc_colocation_new(
	const char *id, enum con_strength strength,
	resource_t *rsc_lh, resource_t *rsc_rh,
	const char *state_lh, const char *state_rh);
	

typedef struct group_variant_data_s
{
		int num_children;
		GListPtr child_list; /* resource_t* */
		resource_t *self;
		resource_t *first_child;
		resource_t *last_child;

		gboolean colocated;
		gboolean ordered;
		
		gboolean child_starting;
		gboolean child_stopping;
		
} group_variant_data_t;


#define get_group_variant_data(data, rsc)				\
	CRM_ASSERT(rsc != NULL);					\
	CRM_ASSERT(rsc->variant == pe_group);				\
	CRM_ASSERT(rsc->variant_opaque != NULL);			\
	data = (group_variant_data_t *)rsc->variant_opaque;		\

void group_unpack(resource_t *rsc, pe_working_set_t *data_set)
{
	resource_t *self = NULL;
	crm_data_t *xml_obj = rsc->xml;
	crm_data_t *xml_self = copy_xml(rsc->xml);
	group_variant_data_t *group_data = NULL;
	const char *group_ordered = g_hash_table_lookup(
		rsc->parameters, XML_RSC_ATTR_ORDERED);
	const char *group_colocated = g_hash_table_lookup(
		rsc->parameters, "collocated");
	

	crm_debug_3("Processing resource %s...", rsc->id);
/* 	rsc->id = "dummy_group_rsc_id"; */

	crm_malloc0(group_data, sizeof(group_variant_data_t));
	group_data->num_children = 0;
	group_data->self	 = NULL;
	group_data->child_list   = NULL;
	group_data->first_child  = NULL;
	group_data->last_child   = NULL;
	rsc->variant_opaque = group_data;

	group_data->ordered   = TRUE;
	group_data->colocated = TRUE;

	if(group_ordered != NULL) {
		cl_str_to_boolean(group_ordered, &(group_data->ordered));
	}
	if(group_colocated != NULL) {
		cl_str_to_boolean(group_colocated, &(group_data->colocated));
	}
	
	/* this is a bit of a hack - but simplifies everything else */
	ha_msg_mod(xml_self, F_XML_TAGNAME, XML_CIB_TAG_RESOURCE);
/* 	set_id(xml_self, "self", -1); */

	if(common_unpack(xml_self, &self, NULL,  data_set)) {
		group_data->self = self;
		self->restart_type = pe_restart_restart;

	} else {
		crm_log_xml_err(xml_self, "Couldnt unpack dummy child");
		return;
	}

	xml_child_iter_filter(
		xml_obj, xml_native_rsc, XML_CIB_TAG_RESOURCE,

		resource_t *new_rsc = NULL;
		if(common_unpack(xml_native_rsc, &new_rsc,
				 group_data->self->parameters, data_set) == FALSE) {
			pe_err("Failed unpacking resource %s",
				crm_element_value(xml_obj, XML_ATTR_ID));
			continue;
		}
		
		crm_free(new_rsc->graph_name);
		if(data_set->short_rsc_names) {
			new_rsc->graph_name = crm_strdup(new_rsc->id);
		} else {
			new_rsc->graph_name = crm_concat(
				group_data->self->id, new_rsc->id, ':');
		}

		new_rsc->parent = rsc;
		group_data->num_children++;
		group_data->child_list = g_list_append(
			group_data->child_list, new_rsc);
		
		if(group_data->first_child == NULL) {
			group_data->first_child = new_rsc;
			
		} else if(group_data->colocated) {
			rsc_colocation_new(
				"pe_group_internal_colo", pecs_must,
				group_data->first_child, new_rsc,
				NULL, NULL);
		}
		group_data->last_child = new_rsc;
		print_resource(LOG_DEBUG_3, "Added", new_rsc, FALSE);
		);
	crm_debug_3("Added %d children to resource %s...",
		    group_data->num_children, group_data->self->id);
	
}


resource_t *
group_find_child(resource_t *rsc, const char *id)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);
	return pe_find_resource(group_data->child_list, id);
}

GListPtr group_children(resource_t *rsc)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);
	return group_data->child_list;
}

int group_num_allowed_nodes(resource_t *rsc)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);
	if(group_data->colocated == FALSE) {
		pe_config_err("Cannot clone non-colocated group: %s", rsc->id);
		return 0;
	}
 	return group_data->self->fns->num_allowed_nodes(group_data->self);
}

color_t *
group_color(resource_t *rsc, pe_working_set_t *data_set)
{
	color_t *group_color = NULL;
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		group_color = child_rsc->fns->color(child_rsc, data_set);
		CRM_CHECK(group_color != NULL, continue);
		native_assign_color(rsc, group_color);
		);
	
	return group_color;
}

void group_update_pseudo_status(resource_t *parent, resource_t *child);

void group_create_actions(resource_t *rsc, pe_working_set_t *data_set)
{
	action_t *op = NULL;
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		child_rsc->fns->create_actions(child_rsc, data_set);
		group_update_pseudo_status(rsc, child_rsc);
		);

	op = start_action(group_data->self, NULL, !group_data->child_starting);
	op->pseudo   = TRUE;

	op = custom_action(group_data->self, started_key(group_data->self),
			   CRMD_ACTION_STARTED, NULL,
			   !group_data->child_starting, TRUE, data_set);
	op->pseudo   = TRUE;

	op = stop_action(group_data->self, NULL, !group_data->child_stopping);
	op->pseudo   = TRUE;
	
	op = custom_action(group_data->self, stopped_key(group_data->self),
			   CRMD_ACTION_STOPPED, NULL,
			   !group_data->child_stopping, TRUE, data_set);
	op->pseudo   = TRUE;

	rsc->actions = group_data->self->actions;
}

void
group_update_pseudo_status(resource_t *parent, resource_t *child) 
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, parent);

	if(group_data->child_stopping && group_data->child_starting) {
		return;
	}
	slist_iter(
		action, action_t, child->actions, lpc,

		if(action->optional) {
			continue;
		}
		if(safe_str_eq(CRMD_ACTION_STOP, action->task) && action->runnable) {
			group_data->child_stopping = TRUE;
		} else if(safe_str_eq(CRMD_ACTION_START, action->task) && action->runnable) {
			group_data->child_starting = TRUE;
		}
		
		);
}

void group_internal_constraints(resource_t *rsc, pe_working_set_t *data_set)
{
	resource_t *last_rsc = NULL;
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	custom_action_order(
		group_data->self, stopped_key(group_data->self), NULL,
		group_data->self, start_key(group_data->self), NULL,
		pe_ordering_optional, data_set);

	custom_action_order(
		group_data->self, stop_key(group_data->self), NULL,
		group_data->self, stopped_key(group_data->self), NULL,
		pe_ordering_optional, data_set);

	custom_action_order(
		group_data->self, start_key(group_data->self), NULL,
		group_data->self, started_key(group_data->self), NULL,
		pe_ordering_optional, data_set);
	
	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,

		order_restart(child_rsc);

		if(group_data->ordered == FALSE) {
			order_start_start(
				group_data->self, child_rsc, pe_ordering_optional);

			custom_action_order(
				child_rsc, start_key(child_rsc), NULL,
				group_data->self, started_key(group_data->self), NULL,
				pe_ordering_optional, data_set);

			order_stop_stop(
				group_data->self, child_rsc, pe_ordering_optional);

			custom_action_order(
				child_rsc, stop_key(child_rsc), NULL,
				group_data->self, stopped_key(group_data->self), NULL,
				pe_ordering_optional, data_set);

			continue;
		}
		
		if(last_rsc != NULL) {
			order_start_start(
				last_rsc, child_rsc, pe_ordering_optional);
			order_stop_stop(
				child_rsc, last_rsc, pe_ordering_optional);

			/* recovery */
			child_rsc->restart_type = pe_restart_restart;
			order_start_start(
				last_rsc, child_rsc, pe_ordering_recover);
			order_stop_stop(
				child_rsc, last_rsc, pe_ordering_recover);

		} else {
			custom_action_order(
				child_rsc, stop_key(child_rsc), NULL,
				group_data->self, stopped_key(group_data->self), NULL,
				pe_ordering_optional, data_set);

			order_start_start(group_data->self, child_rsc,
					  pe_ordering_optional);
		}
		
		last_rsc = child_rsc;
		);

	if(group_data->ordered && last_rsc != NULL) {
		custom_action_order(
			last_rsc, start_key(last_rsc), NULL,
			group_data->self, started_key(group_data->self), NULL,
			pe_ordering_optional, data_set);

		order_stop_stop(
			group_data->self, last_rsc, pe_ordering_optional);
	}
		
}


void group_rsc_colocation_lh(
	resource_t *rsc_lh, resource_t *rsc_rh, rsc_colocation_t *constraint)
{
	group_variant_data_t *group_data = NULL;
	
	if(rsc_lh == NULL) {
		pe_err("rsc_lh was NULL for %s", constraint->id);
		return;

	} else if(rsc_rh == NULL) {
		pe_err("rsc_rh was NULL for %s", constraint->id);
		return;
	}
		
	crm_debug_4("Processing constraints from %s", rsc_lh->id);

	get_group_variant_data(group_data, rsc_lh);
	CRM_CHECK(group_data->self != NULL, return);

	if(group_data->colocated) {
		group_data->first_child->fns->rsc_colocation_lh(
			group_data->first_child, rsc_rh, constraint); 
		return;
	}
	
	if(constraint->strength != pecs_must_not) {
		pe_config_err("Cannot colocate resources with"
			      " non-colocated group: %s", rsc_lh->id);
		return;
	} 

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		child_rsc->fns->rsc_colocation_lh(
			child_rsc, rsc_rh, constraint); 
		);
}

void group_rsc_colocation_rh(
	resource_t *rsc_lh, resource_t *rsc_rh, rsc_colocation_t *constraint)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc_rh);
	CRM_CHECK(group_data->self != NULL, return);
	CRM_CHECK(rsc_lh->variant == pe_native, return);

	crm_debug_3("Processing RH of constraint %s", constraint->id);
	print_resource(LOG_DEBUG_3, "LHS", rsc_lh, TRUE);
	
	if(group_data->colocated) {
		group_data->first_child->fns->rsc_colocation_rh(
			rsc_lh, group_data->first_child, constraint); 
		return;
	}
	
	if(constraint->strength != pecs_must_not) {
		pe_config_err("Cannot colocate resources with"
			      " non-colocated group: %s", rsc_rh->id);
		return;
	} 

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		child_rsc->fns->rsc_colocation_rh(
			rsc_lh, child_rsc, constraint); 
		);
}


void group_rsc_order_lh(resource_t *rsc, order_constraint_t *order)
{
	char *stop_id = NULL;
	char *start_id = NULL;
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	crm_debug_3("Processing LH of ordering constraint %d", order->id);

	if(group_data->self == NULL) {
		return;
	}

	stop_id = stop_key(group_data->self);
	start_id = start_key(group_data->self);
	
	if(safe_str_eq(order->lh_action_task, start_id)) {
		crm_free(order->lh_action_task);
		order->lh_action_task = started_key(group_data->self);

	} else if(safe_str_eq(order->lh_action_task, stop_id)) {
		crm_free(order->lh_action_task);
		order->lh_action_task = stopped_key(group_data->self);
	}

	crm_free(start_id);
	crm_free(stop_id);
	
	group_data->self->fns->rsc_order_lh(group_data->self, order);
}

void group_rsc_order_rh(
	action_t *lh_action, resource_t *rsc, order_constraint_t *order)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	crm_debug_3("Processing RH of ordering constraint %d", order->id);

	if(group_data->self == NULL) {
		return;
	}

	group_data->self->fns->rsc_order_rh(lh_action, group_data->self, order);
}

void group_rsc_location(resource_t *rsc, rsc_to_node_t *constraint)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	crm_debug_3("Processing actions from %s", group_data->self->id);

	group_data->self->fns->rsc_location(group_data->self, constraint);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		child_rsc->fns->rsc_location(child_rsc, constraint);
		);
}

void group_expand(resource_t *rsc, pe_working_set_t *data_set)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	crm_debug_3("Processing actions from %s", rsc->id);

	CRM_CHECK(group_data->self != NULL, return);
	group_data->self->fns->expand(group_data->self, data_set);
	
	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,

		child_rsc->fns->expand(child_rsc, data_set);
		);

}

gboolean group_active(resource_t *rsc, gboolean all)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		gboolean child_active = child_rsc->fns->active(child_rsc, all);
		if(all == FALSE && child_active) {
			return TRUE;
		} else if(child_active == FALSE) {
			return FALSE;
		}
		);
	if(all) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void group_print(
	resource_t *rsc, const char *pre_text, long options, void *print_data)
{
	const char *child_text = NULL;
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);
	if(pre_text != NULL) {
		child_text = "        ";
	} else {
		child_text = "    ";
	}
	
	status_print("%sResource Group: %s",
		     pre_text?pre_text:"", rsc->id);

	if(options & pe_print_html) {
		status_print("\n<ul>\n");

	} else if((options & pe_print_log) == 0) {
		status_print("\n");
	}
	
	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		
		if(options & pe_print_html) {
			status_print("<li>\n");
		}
		child_rsc->fns->print(
			child_rsc, child_text, options, print_data);
		if(options & pe_print_html) {
			status_print("</li>\n");
		}
		);

	if(options & pe_print_html) {
		status_print("</ul>\n");
	}
}

void group_free(resource_t *rsc)
{
	group_variant_data_t *group_data = NULL;
	CRM_CHECK(rsc != NULL, return);
	get_group_variant_data(group_data, rsc);

	crm_debug_3("Freeing %s", rsc->id);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,

		crm_debug_3("Freeing child %s", child_rsc->id);
		child_rsc->fns->free(child_rsc);
		);

	crm_debug_3("Freeing child list");
	pe_free_shallow_adv(group_data->child_list, FALSE);

	if(group_data->self != NULL) {
		free_xml(group_data->self->xml);
		group_data->self->fns->free(group_data->self);
	}

	common_free(rsc);
}


void
group_agent_constraints(resource_t *rsc)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		
		child_rsc->fns->agent_constraints(child_rsc);
		);
}

enum rsc_role_e
group_resource_state(resource_t *rsc)
{
	enum rsc_role_e group_role = RSC_ROLE_UNKNOWN;
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,

		if(child_rsc->next_role > group_role) {
			group_role = rsc->next_role;
		}
		if(child_rsc->failed) {
			rsc->failed = TRUE;
		}
		);
	return group_role;
}

void
group_create_notify_element(resource_t *rsc, action_t *op,
			    notify_data_t *n_data, pe_working_set_t *data_set)
{
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		
		child_rsc->fns->create_notify_element(
			child_rsc, op, n_data, data_set);
		);
}

gboolean
group_create_probe(resource_t *rsc, node_t *node, action_t *complete,
		    gboolean force, pe_working_set_t *data_set) 
{
	gboolean any_created = FALSE;
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		
		any_created = child_rsc->fns->create_probe(
			child_rsc, node, complete, force, data_set) || any_created;
		);
	return any_created;
}

void
group_stonith_ordering(
	resource_t *rsc,  action_t *stonith_op, pe_working_set_t *data_set)
{
#if 0
	/* I dont think it is a good idea to be poking inside groups */
	group_variant_data_t *group_data = NULL;
	get_group_variant_data(group_data, rsc);

	slist_iter(
		child_rsc, resource_t, group_data->child_list, lpc,
		
		child_rsc->fns->stonith_ordering(
			child_rsc, stonith_op, data_set);
		);
#endif
	native_stonith_ordering(rsc, stonith_op, data_set);
}
