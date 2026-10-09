/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include "oap_selection.h"
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/ports.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

#define SELECTION_MAGIC 0x4f415053UL
#define SELECTION_VERSION 1
/* One-way Exec message. AllocVec(MEMF_PUBLIC) storage and the inline payload
 * transfer to the recipient at PutMsg. The sender never touches them again;
 * the recipient frees them at GetMsg or listener teardown. No sender stack
 * pointers and no reply-port lifetime dependency. */
typedef struct SelectionMessage {
    struct Message message;
    ULONG magic;
    UWORD version;
    char uri[OAP_SELECTION_URI_MAX];
} SelectionMessage;

static int subscriber(const struct Node *node)
{
    return node->ln_Name &&
        !strncmp(node->ln_Name,OAP_SELECTION_PREFIX,strlen(OAP_SELECTION_PREFIX));
}
static int valid_uri(const char *uri)
{
    size_t i,scheme;
    if(!uri)return 0;
    if(!strncmp(uri,"ipp://",6))scheme=6;
    else if(!strncmp(uri,"ipps://",7))scheme=7;
    else return 0;
    for(i=0;i<OAP_SELECTION_URI_MAX;i++){
        unsigned char c=(unsigned char)uri[i];
        if(!c)return i>scheme;
        if(c<=32 || c==127 || c=='"' || c=='*')return 0;
    }
    return 0;
}
static int owned_message(struct Message *message)
{
    SelectionMessage *m=(SelectionMessage *)message;
    return message->mn_Length==sizeof(*m) && !message->mn_ReplyPort &&
        m->magic==SELECTION_MAGIC && m->version==SELECTION_VERSION;
}
static void release_message(struct Message *message)
{
    if(owned_message(message))FreeVec(message);
    else if(message->mn_ReplyPort)ReplyMsg(message);
}
int oap_selection_open(OAPSelectionListener *listener)
{
    static unsigned long serial;
    struct Node *node;
    unsigned count=0;
    int ok=0;
    if(!listener)return 0;
    memset(listener,0,sizeof(*listener));
    listener->port=CreateMsgPort();
    if(!listener->port)return 0;
    Forbid();
    for(node=SysBase->PortList.lh_Head;node->ln_Succ;node=node->ln_Succ)
        if(subscriber(node))count++;
    if(count<OAP_SELECTION_MAX_LISTENERS){
        do{
            snprintf(listener->name,sizeof(listener->name),
                     OAP_SELECTION_PREFIX "%08lx.%08lx",
                     (unsigned long)listener->port,++serial);
        }while(FindPort((STRPTR)listener->name));
        listener->port->mp_Node.ln_Name=listener->name;
        listener->port->mp_Node.ln_Pri=1;
        AddPort(listener->port);
        ok=1;
    }
    Permit();
    if(!ok){DeleteMsgPort(listener->port);listener->port=NULL;}
    return ok;
}
unsigned long oap_selection_mask(const OAPSelectionListener *listener)
{
    return listener && listener->port ? 1UL<<listener->port->mp_SigBit:0;
}
int oap_selection_receive(OAPSelectionListener *listener,char *uri,size_t cap)
{
    struct Message *message;
    int received=0;
    if(!listener || !listener->port)return 0;
    while((message=GetMsg(listener->port))!=NULL){
        SelectionMessage *m=(SelectionMessage *)message;
        if(owned_message(message) && valid_uri(m->uri) && uri &&
           strlen(m->uri)<cap){
            strcpy(uri,m->uri);
            received=1; /* Deliberately true even for the same URI twice. */
        }
        release_message(message);
    }
    return received;
}
void oap_selection_close(OAPSelectionListener *listener)
{
    if(!listener || !listener->port)return;
    /* Unpublish first; concurrent publishers use FindPort+PutMsg under Forbid.
     * After RemPort no new selection message can enter this queue. */
    Forbid();RemPort(listener->port);Permit();
    oap_selection_receive(listener,NULL,0);
    DeleteMsgPort(listener->port);
    listener->port=NULL;
}
int oap_selection_publish(const char *uri)
{
    char (*names)[OAP_SELECTION_NAME_MAX];
    struct Node *node;
    size_t count=0,i;
    int sent=0,failed=0;
    if(!valid_uri(uri))return -1;
    names=AllocVec(OAP_SELECTION_MAX_LISTENERS*sizeof(*names),MEMF_PUBLIC|MEMF_CLEAR);
    if(!names)return -1;
    /* Snapshot names only. Never hold a foreign port pointer across Permit,
     * and never allocate, access files or wait with scheduling forbidden. */
    Forbid();
    for(node=SysBase->PortList.lh_Head;node->ln_Succ;node=node->ln_Succ){
        if(subscriber(node) && count<OAP_SELECTION_MAX_LISTENERS){
            strncpy(names[count],node->ln_Name,OAP_SELECTION_NAME_MAX-1);
            count++;
        }
    }
    Permit();
    for(i=0;i<count;i++){
        struct MsgPort *port;
        SelectionMessage *m=AllocVec(sizeof(*m),MEMF_PUBLIC|MEMF_CLEAR);
        if(!m){failed=1;continue;}
        m->message.mn_Length=sizeof(*m);
        m->magic=SELECTION_MAGIC;m->version=SELECTION_VERSION;
        strcpy(m->uri,uri);
        Forbid();
        port=FindPort((STRPTR)names[i]);
        if(port)PutMsg(port,&m->message);
        Permit();
        if(port)sent++;else FreeVec(m);
    }
    FreeVec(names);
    return failed?-1:sent;
}
