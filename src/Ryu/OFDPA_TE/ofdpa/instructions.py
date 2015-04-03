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
# Implements Instructions object
#
from ofdpa.actions import Actions
from ofdpa.utils import Utils
#
class Instructions():

    def __init__(self, *args, **kwargs):
        super(Instructions, self).__init__(*args, **kwargs)

    @staticmethod
    def create_instructions(dp, config):
        #
        instructions = []
        #
        # instruction config is a list
        print "instructions config: %s" % config
        for instruction in config:
            for key in instruction.keys():
                #
                val = instruction[key]
                #
                #print "key: %s" % key
                #print "val: %s" % val
                #
                if (key  == "apply"):
                    instructions.append(Instructions.process_apply(dp,val))

                elif (key  == "write"):
                    instructions.append(Instructions.process_write(dp,val))

                elif (key  == "goto"):
                    instructions.append(Instructions.process_goto(dp,val))

                elif (key  == "meter"):
                    instructions.append(Instructions.process_meter(dp,val))

                elif (key  == "metadata"):
                    instructions.append(Instructions.process_metadata(dp,val))

                elif (key  == "clear"):
                    instructions.append(Instructions.process_clear(dp,val))

                else:
                    raise Exception("Wrong instruction name:", key)

        print "instructions: %s" % instructions
        return instructions

    @staticmethod
    def process_apply(dp,config):
        #
        type = dp.ofproto.OFPIT_APPLY_ACTIONS
        #
        for entry in config:
            actions = Actions.create_actions(dp, entry["actions"])
        return dp.ofproto_parser.OFPInstructionActions(type,actions)
        
    @staticmethod
    def process_write(dp,config):
        print config
        type = dp.ofproto.OFPIT_WRITE_ACTIONS
        #
        for entry in config:
            actions = Actions.create_actions(dp, entry["actions"])
        return dp.ofproto_parser.OFPInstructionActions(type,actions)

    @staticmethod
    def process_clear(dp,config):
        type = dp.ofproto.OFPIT_CLEAR_ACTIONS
        #
        for entry in config:
            actions = Actions.create_actions(dp, entry["actions"])
        return dp.ofproto_parser.OFPInstructionActions(type,actions)

    @staticmethod
    def process_goto(dp,config):
        table = Utils.get_table(dp, config["table"])
        return dp.ofproto_parser.OFPInstructionGotoTable(table)

    @staticmethod
    def process_meter(dp,config):
        meter_id = int(config["meter_id"])
        return dp.ofproto_parser.OFPInstructionMeter(meter_id)

    @staticmethod
    def process_metadata(dp,config):
        metadata = int(config["metadata"])
        mask = int(config["mask"])
        return dp.ofproto_parser.OFPInstructionWriteMetadata(metadata,mask)

