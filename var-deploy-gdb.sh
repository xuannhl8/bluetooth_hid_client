#!/bin/bash
readonly TARGET_IP="$1"
readonly PROGRAM="$2"
readonly TARGET_DIR="/home/root"

# ssh -i /home/dinhle/.ssh/id_rsa root@${TARGET_IP} "sh -c '/usr/bin/killall -q gdbserver; rm -rf ${TARGET_DIR}/${PROGRAM} exit 0'"

ssh -i /home/dinh/.ssh/id_rsa root@${TARGET_IP} "sh -c '/usr/bin/killall -q gdbserver; exit 0'"

echo "Starting GDB Server on Target"

ssh -t -i /home/dinhle/.ssh/id_rsa root@${TARGET_IP} "sh -c 'cd ${TARGET_DIR}; gdbserver localhost:3000 ${PROGRAM}'"