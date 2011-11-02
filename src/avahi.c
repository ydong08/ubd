#include <config.h>
#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>
#include <gio/gio.h>

#include "config.h"

//#include <avahi-client/client.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>
#include "avahi-glib/glib-watch.h"
#include "avahi-glib/glib-malloc.h"
#include <avahi-common/alternative.h>
#include <avahi-common/address.h>
#include <avahi-core/core.h>
#include <avahi-core/publish.h>

#include <syslog.h>

#include "avahi.h"
#include "nodes.h"
#include "classes.h"
#include "groups.h"
#include "debug.h"

const AvahiPoll *poll_api;
AvahiGLibPoll *glib_poll;
//AvahiClient *client;
GMainLoop *loop;
AvahiServer *server;
AvahiServerConfig serverconfig;

static void avahi_server_callback(AvahiServer *s,
                AvahiServerState state, void* userdata )
{
    GMainLoop *loop = userdata;
    syslog(LOG_INFO, "Avahi Server State Change: %d", state);

    if (state == AVAHI_SERVER_FAILURE)
    {
        syslog(LOG_INFO, "Error in server: %s",
                avahi_strerror(avahi_server_errno(s)));

        g_main_loop_quit (loop);
    }

}

static void address_group_callback(AvahiServer *s, AvahiSEntryGroup *group,
        AvahiEntryGroupState state, void *userdata) {
    /* Called whenever the entry group state changes */
    ub_assert(userdata);
    struct node *n = userdata;
    gchar *name = n->hostname;

    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            /* The entry group has been established successfully */
            syslog(LOG_INFO, "Address '%s' successfully established.\n", name);
            break;

        case AVAHI_ENTRY_GROUP_COLLISION : 
            /* An address collision with a remote address
             * happened.*/

            syslog(LOG_ERR, "Address collision for %s\n", name);
            syslog(LOG_ERR, "Is another ubd running in the local network?\n");
            g_main_loop_quit (loop);
        break;

        case AVAHI_ENTRY_GROUP_FAILURE :
            syslog(LOG_ERR, "Entry group failure: %s\n",
                avahi_strerror(avahi_server_errno(s)));

            g_main_loop_quit (loop);
            break;

        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            ;
    }
}

static void entry_group_callback(AvahiServer *s, AvahiSEntryGroup *group,
        AvahiEntryGroupState state, void *userdata) {

    /* Called whenever the entry group state changes */
    ub_assert(userdata);
    struct socketdata *sd = userdata;
    gchar *name = sd->avahiservicename;

    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            /* The entry group has been established successfully */
            syslog(LOG_INFO, "Service '%s' successfully established.\n", name);
            break;

        case AVAHI_ENTRY_GROUP_COLLISION : {
            char *n;

            /* A service name collision with a remote service
             * happened. Let's pick a new name */
            n = avahi_alternative_service_name(name);
            avahi_free(name);
            name = n;

            syslog(LOG_ERR, "Service name collision,"
                                "renaming service to '%s'\n", name);
            while(1);
            /* And recreate the services */
            //create_services(avahi_entry_group_get_client(g));
            break;
        }

        case AVAHI_ENTRY_GROUP_FAILURE :
            syslog(LOG_ERR, "Entry group failure: %s\n",
                avahi_strerror(avahi_server_errno(server)));

            while(1);
            //g_main_loop_quit (loop);
            break;

        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            ;
    }
}

static void multicast_group_callback(AvahiServer *s, AvahiSEntryGroup *group,
        AvahiEntryGroupState state, void *userdata)
{
    /* Called whenever the entry group state changes */
    ub_assert(userdata);
    struct multicastgroup *g = userdata;
    gchar *name = g->avahiservicename;

    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            /* The entry group has been established successfully */
            syslog(LOG_INFO, "Multicast group '%s' successfully established.\n",
                    name);
            break;

        case AVAHI_ENTRY_GROUP_COLLISION : {
            char *n;

            /* A service name collision with a remote service
             * happened. Let's pick a new name */
            n = avahi_alternative_service_name(name);
            avahi_free(name);
            name = n;

            syslog(LOG_ERR, "Service name collision,"
                                "renaming service to '%s'\n", name);
            while(1);
            /* And recreate the services */
            //create_services(avahi_entry_group_get_client(g));
            break;
        }

        case AVAHI_ENTRY_GROUP_FAILURE :
            syslog(LOG_ERR, "Entry group failure: %s\n",
                avahi_strerror(avahi_server_errno(s)));

            while(1);
            //g_main_loop_quit (loop);
            break;

        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            ;
    }
}

void avahi_init(GMainLoop *mainloop)
{
    int error;

    loop = mainloop;
    avahi_set_allocator(avahi_glib_allocator());
    glib_poll = avahi_glib_poll_new(NULL, G_PRIORITY_DEFAULT);
    poll_api = avahi_glib_poll_get(glib_poll);
    avahi_server_config_init(&serverconfig);

    server = avahi_server_new(poll_api,
            &serverconfig,
            avahi_server_callback,
            mainloop,
            &error); 

    if( server == NULL ){
        g_warning("Error initializing Avahi: %s", avahi_strerror(error));
        g_main_loop_quit(loop);
    }
}

void avahi_addService(struct node *n, guint classid)
{
    int ret;

    guint class = n->classes[classid];
    struct socketdata *sd = &(n->tcpsockets[classid]);
    ub_assert(sd->avahiservicename == NULL);
    sd->avahiservicename = avahi_strdup(classes_getTcpServiceName(class));

    ub_assert(sd->avahiservicegroup == NULL);
    sd->avahiservicegroup =
            avahi_s_entry_group_new(server, entry_group_callback, sd);
    ub_assert(sd->avahiservicegroup != NULL);

    if( (ret = avahi_server_add_service(
                server,
                sd->avahiservicegroup,
                AVAHI_IF_UNSPEC,
                AVAHI_PROTO_UNSPEC,
                0,
                n->avahiname,
                sd->avahiservicename,
                NULL,
                n->hostname,
                classes_getServicePort(class),
                NULL)) < 0 ){
        syslog(LOG_ERR, "Failed to add service: %s\n", avahi_strerror(ret));
    }
    if( (ret = avahi_s_entry_group_commit(sd->avahiservicegroup)) < 0 ){
        syslog(LOG_ERR, "Failed to commit entry group: %s\n",
            avahi_strerror(ret));
        while(1);
    }

    sd = &(n->udpsockets[classid]);
    ub_assert(sd->avahiservicename == NULL);
    sd->avahiservicename = avahi_strdup(classes_getUdpServiceName(class));

    ub_assert(sd->avahiservicegroup == NULL);
    sd->avahiservicegroup =
            avahi_s_entry_group_new(server, entry_group_callback, sd);
    ub_assert(sd->avahiservicegroup != NULL);

    if( (ret = avahi_server_add_service(
                server,
                sd->avahiservicegroup,
                AVAHI_IF_UNSPEC,
                AVAHI_PROTO_UNSPEC,
                0,
                n->avahiname,
                sd->avahiservicename,
                NULL,
                n->hostname,
                classes_getServicePort(class),
                NULL)) < 0 ){
        syslog(LOG_ERR, "Failed to add service: %s\n", avahi_strerror(ret));
    }
    if( (ret = avahi_s_entry_group_commit(sd->avahiservicegroup)) < 0 ){
        syslog(LOG_ERR, "Failed to commit entry group: %s\n",
            avahi_strerror(ret));
        while(1);
    }
}

void avahi_removeService(struct node *n, guint classid)
{
    struct socketdata *sd = &(n->tcpsockets[classid]);

    if( sd->avahiservicegroup )
        avahi_s_entry_group_reset(sd->avahiservicegroup);
    sd->avahiservicegroup = NULL;

    if( sd->avahiservicename )
        avahi_free(sd->avahiservicename);
    sd->avahiservicename = NULL;

    sd = &(n->udpsockets[classid]);

    if( sd->avahiservicegroup )
        avahi_s_entry_group_reset(sd->avahiservicegroup);
    sd->avahiservicegroup = NULL;

    if( sd->avahiservicename != NULL )
        avahi_free(sd->avahiservicename);
    sd->avahiservicename = NULL;
}

void avahi_registerServices(struct node *n)
{
    guint i;
    ub_assert(n->avahiaddressgroup != NULL);

    for(i=0; i<sizeof(n->classes); i++){
        if( n->classes[i] != 0 ){
            syslog(LOG_DEBUG,"adding service %d\n", n->classes[i]);
            avahi_addService(n, i);
        }
    }
}

void avahi_removeServices(struct node *n)
{
    guint i;
    ub_assert(n->avahiaddressgroup != NULL);

    syslog(LOG_DEBUG,"removing services for node %s", n->id);

    for(i=0; i<sizeof(n->classes); i++){
        if( n->classes[i] != 0 ){
            syslog(LOG_DEBUG,"removing service %d\n", n->classes[i]);
            avahi_removeService(n, i);
        }
    }
}


void avahi_registerNode(struct node *n)
{
    int ret;
    AvahiAddress a;
    
    char *address = g_inet_address_to_string(n->netadr);
    avahi_address_parse(address, AVAHI_PROTO_UNSPEC , &a);
    g_free(address);
    
    ub_assert(n->avahiaddressgroup == NULL);
    n->avahiaddressgroup =
            avahi_s_entry_group_new(server, address_group_callback, n);
    if( (ret = avahi_server_add_address(
                server,
                n->avahiaddressgroup,
                AVAHI_IF_UNSPEC,
                AVAHI_PROTO_UNSPEC,
                0,
                n->hostname,
                &a )) < 0 ){
        syslog(LOG_ERR, "Failed to add address: %s\n", avahi_strerror(ret));
    }

    if( (ret = avahi_s_entry_group_commit(n->avahiaddressgroup)) < 0 ){
        syslog(LOG_ERR, "Failed to commit entry group: %s\n",
            avahi_strerror(ret));
        while(1);
    }
    ub_assert(n->avahiaddressgroup != NULL);
}

void avahi_removeNode(struct node *n)
{
    if(n->avahiaddressgroup != NULL)
        avahi_s_entry_group_reset(n->avahiaddressgroup);
    n->avahiaddressgroup = NULL;
}

void avahi_registerMulticastGroup(struct multicastgroup *g)
{
    printf("avahi_registerMulticastGroup()\n");
    int ret;
    AvahiAddress a;
    
    char *address = g_inet_address_to_string(
                    g_inet_socket_address_get_address(
                    (GInetSocketAddress*)g->sa));
    avahi_address_parse(address, AVAHI_PROTO_UNSPEC , &a);
    g_free(address);
    
    ub_assert(g->avahientrygroup == NULL);
    g->avahientrygroup =
            avahi_s_entry_group_new(server, multicast_group_callback, g);
    ub_assert(g->avahientrygroup != NULL);
    if( (ret = avahi_server_add_address(
                server,
                g->avahientrygroup,
                AVAHI_IF_UNSPEC,
                AVAHI_PROTO_UNSPEC,
                0,
                g->hostname,
                &a )) < 0 ){
        syslog(LOG_ERR, "Failed to add address: %s\n", avahi_strerror(ret));
    }

    guint class = g->class;
    ub_assert(g->avahiservicename == NULL);
    g->avahiservicename = avahi_strdup(classes_getUdpServiceName(class));

    if( (ret = avahi_server_add_service(
                server,
                g->avahientrygroup,
                AVAHI_IF_UNSPEC,
                AVAHI_PROTO_UNSPEC,
                0,
                g->avahiname,
                g->avahiservicename,
                NULL,
                g->hostname,
                classes_getServicePort(class),
                NULL)) < 0 ){
        syslog(LOG_ERR, "Failed to add service: %s\n", avahi_strerror(ret));
    }

    if( (ret = avahi_s_entry_group_commit(g->avahientrygroup)) < 0 ){
        syslog(LOG_ERR, "Failed to commit entry group: %s\n",
            avahi_strerror(ret));
        while(1);
    }
}
