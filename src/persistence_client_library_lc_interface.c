/******************************************************************************
 * Project         Persistency
 * (c) copyright   2012
 * Company         XS Embedded GmbH
 *****************************************************************************/
/******************************************************************************
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License, v. 2.0. If a  copy of the MPL was not distributed
 * with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
******************************************************************************/
 /**
 * @file           persistence_client_library_lc_interface.c
 * @ingroup        Persistence client library
 * @author         Ingo Huerner
 * @brief          Implementation of the persistence client library lifecycle interface.
 * @see
 */


#include "persistence_client_library_lc_interface.h"

#include "../include_protected/persistence_client_library_data_organization.h"
#include "../include_protected/persistence_client_library_db_access.h"

#include "persistence_client_library_handle.h"
#include "persistence_client_library_pas_interface.h"
#include "persistence_client_library_dbus_service.h"
#include "persistence_client_library_custom_loader.h"
#include "persistence_client_library_prct_access.h"
#include "persistence_client_library_itzam_errors.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

static int gTimeoutMs = 50000;

int check_lc_request(int request, int requestID)
{
   int rval = 0;

   switch(request)
   {
      case NsmShutdownNormal:
      {
         uint64_t cmd;
         // add command and data to queue
         cmd = ( ((uint64_t)requestID << 32) | ((uint64_t)request << 16) | CMD_LC_PREPARE_SHUTDOWN);

         if(-1 == write(gEfds, &cmd, (sizeof(uint64_t))))
         {
            DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("check_lc_request => failed to write to pipe"), DLT_INT(errno));
            rval = NsmErrorStatus_Fail;
         }
         else
         {
            rval = NsmErrorStatus_OK;
         }
         break;
      }
      default:
      {
         DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("check_lc_request => Unknown lifecycle message"), DLT_INT(request));
         break;
      }
   }

   return rval;
}


int msg_lifecycleRequest(DBusConnection *connection, DBusMessage *message)
{
   int request   = 0,
       requestID = 0,
       msgReturn = 0;

   DBusMessage *reply;
   DBusError error;
   dbus_error_init (&error);

   if (!dbus_message_get_args (message, &error, DBUS_TYPE_UINT32, &request,
                                                DBUS_TYPE_UINT32, &requestID,
                                                DBUS_TYPE_INVALID))
   {
      reply = dbus_message_new_error(message, error.name, error.message);

      if (reply == 0)
      {
         DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("msg_lifecycleRequest => DBus No memory"));
      }

      if (!dbus_connection_send(connection, reply, 0))
      {
         DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("msg_lifecycleRequest => DBus No memory"));
      }

      dbus_message_unref(reply);

      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
   }

   msgReturn = check_lc_request(request, requestID);

   reply = dbus_message_new_method_return(message);

   if (reply == 0)
   {
      DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("msg_lifecycleRequest => DBus No memory"));
   }

   if (!dbus_message_append_args(reply, DBUS_TYPE_INT32, &msgReturn, DBUS_TYPE_INVALID))
   {
      DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("msg_lifecycleRequest => DBus No memory"));
   }

   if (!dbus_connection_send(connection, reply, 0))
   {
      DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("msg_lifecycleRequest => DBus No memory"));
   }

   dbus_connection_flush(connection);
   dbus_message_unref(reply);

   return DBUS_HANDLER_RESULT_HANDLED;
}




DBusHandlerResult checkLifecycleMsg(DBusConnection * connection, DBusMessage * message, void * user_data)
{
   DBusHandlerResult result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

   if((0==strncmp("org.genivi.NodeStateManager.LifeCycleConsumer", dbus_message_get_interface(message), 20)))
   {
      if((0==strncmp("LifecycleRequest", dbus_message_get_member(message), 18)))
      {
         result = msg_lifecycleRequest(connection, message);
      }
      else
      {
          DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("checkLifecycleMsg -> unknown message "), DLT_STRING(dbus_message_get_interface(message)));
      }
   }
   return result;
}



int send_lifecycle_register(const char* method, int shutdownMode, int reg)
{
   int rval = 0;

   DBusError error;
   dbus_error_init (&error);
   DBusConnection* conn = get_dbus_connection();

   if(conn != NULL)
   {
      const char* objName = "/org/genivi/NodeStateManager/LifeCycleConsumer";
      const char* busName = dbus_bus_get_unique_name(conn);

      DBusMessage* message = dbus_message_new_method_call("org.genivi.NodeStateManager",           // destination
                                                          "/org/genivi/NodeStateManager/Consumer", // path
                                                          "org.genivi.NodeStateManager.Consumer",  // interface
                                                          method);                                 // method
      if(message != NULL)
      {
         if(reg == 1)   // register
         {
            dbus_message_append_args(message, DBUS_TYPE_STRING, &busName,
                                              DBUS_TYPE_STRING, &objName,
                                              DBUS_TYPE_UINT32, &shutdownMode,
                                              DBUS_TYPE_UINT32, &gTimeoutMs, DBUS_TYPE_INVALID);
         }
         else           // unregister
         {
            dbus_message_append_args(message, DBUS_TYPE_STRING, &busName,
                                              DBUS_TYPE_STRING, &objName,
                                              DBUS_TYPE_UINT32, &shutdownMode, DBUS_TYPE_INVALID);

         }

         if(conn != NULL)
         {
            if(!dbus_connection_send(conn, message, 0))
            {
               DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("send_lifecycle_register => Access denied"), DLT_STRING(error.message) );
               rval = -1;
            }

            dbus_connection_flush(conn);
         }
         else
         {
            DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("send_lifecycle_register => ERROR: Invalid connection"));
            rval = -1;
         }
         dbus_message_unref(message);
      }
      else
      {
         DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("send_lifecycle_register => ERROR: Invalid message"));
         rval = -1;
      }
   }
   else
   {
      DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("send_lifecycle_register => ERROR: connection isn NULL"));
      rval = -1;
   }

   return rval;
}



int send_lifecycle_request(const char* method, int requestId, int status)
{
   int rval = 0;

   DBusError error;
   dbus_error_init (&error);

   DBusConnection* conn = get_dbus_connection();

   if(conn != NULL)
   {
      DBusMessage* message = dbus_message_new_method_call("org.genivi.NodeStateManager",           // destination
                                                         "/org/genivi/NodeStateManager/Consumer",  // path
                                                          "org.genivi.NodeStateManager.Consumer",  // interface
                                                          method);                  // method
      if(message != NULL)
      {
         dbus_message_append_args(message, DBUS_TYPE_INT32, &requestId,
                                           DBUS_TYPE_INT32, &status,
                                           DBUS_TYPE_INVALID);

         if(conn != NULL)
         {
            if(!dbus_connection_send(conn, message, 0))
            {
               DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("send_lifecycle_request => Access denied"), DLT_STRING(error.message) );
               rval = -1;
            }

            dbus_connection_flush(conn);
         }
         else
         {
            DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("send_lifecycle_request => ERROR: Invalid connection"));
            rval = -1;
         }
         dbus_message_unref(message);
      }
      else
      {
         DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("send_lifecycle_request => ERROR: Invalid message"));
         rval = -1;
      }
   }
   else
   {
      DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("send_lifecycle_request => ERROR: connection isn NULL"));
      rval = -1;
   }

   return rval;
}



int register_lifecycle(int shutdownMode)
{
   return send_lifecycle_register("RegisterShutdownClient", shutdownMode, 1);
}



int unregister_lifecycle(int shutdownMode)
{
   return send_lifecycle_register("UnRegisterShutdownClient", shutdownMode, 0);
}


int send_prepare_shutdown_complete(int requestId, int status)
{
   return send_lifecycle_request("LifecycleRequestComplete", requestId, status);
}




void process_prepare_shutdown(unsigned char requestId, unsigned int status)
{
   int i = 0;
   //GvdbTable* resourceTable = NULL;
   itzam_btree* resourceTable = NULL;
   itzam_state  state = ITZAM_FAILED;

   // block write
   pers_lock_access();

   // flush open files to disk
   for(i=0; i<MaxPersHandle; i++)
   {
      int tmp = i;
      if(gOpenFdArray[tmp] == FileOpen)
      {
         fsync(tmp);
         close(tmp);
      }
   }

   // close open gvdb persistence resource configuration table
   for(i=0; i< PrctDbTableSize; i++)
   {
     resourceTable = get_resource_cfg_table_by_idx(i);
     // dereference opend database
     if(resourceTable != NULL)
     {
        state = itzam_btree_close(resourceTable);
        if (state != ITZAM_OKAY)
        {
           DLT_LOG(gDLTContext, DLT_LOG_ERROR, DLT_STRING("process_prepare_shutdown => itzam_btree_close: Itzam problem"), DLT_STRING(STATE_MESSAGES[state]));
        }
     }
   }

   //close opend database
   pers_db_close_all();


   // unload custom client libraries
   for(i=0; i<PersCustomLib_LastEntry; i++)
   {
      if(gPersCustomFuncs[i].custom_plugin_deinit != NULL)
      {
         // deinitialize plugin
         gPersCustomFuncs[i].custom_plugin_deinit();
         // close library handle
         dlclose(gPersCustomFuncs[i].handle);
      }
   }

   // notify lifecycle shutdown OK
   send_prepare_shutdown_complete((int)requestId, (int)status);
}

