#*********************************************************************
#
# (C) Copyright Broadcom Corporation 2013-2014
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#
#*********************************************************************

#
# Different useful staff
#
class Utils():

    def __init__(self, *args, **kwargs):
        super(Utils, self).__init__(*args, **kwargs)

    @staticmethod
    def to_int(str):
        if len(str) < 2:
            return int(str)
        elif str[0:2] in ("0x","0X"):
            return int(str,16)
        else:
            return int(str)

    @staticmethod
    def get_table(dp, table_name):
        #
        TABLE_INGRESS = 0
        TABLE_VLAN = 10
        TABLE_MAC = 20
        TABLE_UNICAST = 30
        TABLE_MULTICAST = 40
        TABLE_BRIDGING = 50
        TABLE_ACL = 60
        #
        #print "table_name: %s" % table_name
        #
        if (table_name == "port"):
            return TABLE_INGRESS

        elif (table_name == "vlan"):
            return TABLE_VLAN

        elif (table_name == "mac"):
            return TABLE_MAC

        elif (table_name == "unicast"):
            return TABLE_UNICAST

        elif (table_name == "multicast"):
            return TABLE_MULTICAST

        elif (table_name == "bridging"):
            return TABLE_BRIDGING

        elif (table_name == "acl"):
            return TABLE_ACL

        elif (table_name == "all"):
            return dp.ofproto.OFPTT_ALL

        else:
            raise Exception("Wrong table name", table_name)

    @staticmethod
    def get_mod_flow_command(dp, cmd):
        #
        if(cmd == "add"):
            return dp.ofproto.OFPFC_ADD

        elif(cmd == "mod"):
            return dp.ofproto.OFPFC_MODIFY

        elif(cmd == "mods"):
            return dp.ofproto.OFPFC_MODIFY_STRICT

        elif(cmd == "del"):
            return dp.ofproto.OFPFC_DELETE

        elif(cmd == "dels"):
            return dp.ofproto.OFPFC_DELETE_STRICT

        else:
            raise Exception("Wrong flow command", cmd)

    @staticmethod
    def get_mod_group_command(dp, cmd):
        #
        if(cmd == "add"):
            return dp.ofproto.OFPGC_ADD

        elif(cmd == "mod"):
            return dp.ofproto.OFPGC_MODIFY

        elif(cmd == "del"):
            return dp.ofproto.OFPGC_DELETE

        else:
            raise Exception("Wrong group command", cmd)

    @staticmethod
    def get_mod_group(dp, group):
        #
        if(group == "all"):
            return dp.ofproto.OFPG_ALL

        if(group == "any"):
            return dp.ofproto.OFPG_ANY

        else:
            return Utils.to_int(group)

    @staticmethod
    def get_mod_type(dp, type):
        #
        if(type == "all"):
            return dp.ofproto.OFPGT_ALL

        elif(type == "select"):
            return dp.ofproto.OFPGT_SELECT

        elif(type == "indirect"):
            return dp.ofproto.OFPGT_INDIRECT

        elif(type == "ff"):
            return dp.ofproto.OFPGT_FF

        else:
            raise Exception("Wrong group type", type)

    @staticmethod
    def get_mod_port(dp, port):
        #
        if (port == "in"):
            return dp.ofproto.OFPP_IN_PORT

        elif (port == "table"):
            return dp.ofproto.OFPP_TABLE

        elif (port == "normal"):
            return dp.ofproto.OFPP_NORMAL

        elif (port == "flood"):
            return dp.ofproto.OFPP_FLOOD

        elif (port == "all"):
            return dp.ofproto.OFPP_ALL

        elif (port == "controller"):
            return dp.ofproto.OFPP_CONTROLLER

        elif (port == "local"):
            return dp.ofproto.OFPP_LOCAL

        elif (port == "any"):
            return dp.ofproto.OFPP_ANY

        else:
            return Utils.to_int(port)
