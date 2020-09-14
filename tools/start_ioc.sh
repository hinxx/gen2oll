#!/bin/bash
#
# Author  : Hinko Kocevar
# Created : 11 Sep 2019

set -e
set -u

if [[ $# -lt 3 ]]; then
  echo "usage $(basename $0) <dev|gdb|systemd> <top> <instance> <telnet_port>"
  exit 1
fi

# MODE of operation
MODE="$1"
# TOP_DIR is root folder of IOC
TOP_DIR="$(realpath $2)"
# INSTANCE_NAME is instance name (sub-folder of ioc/)
INSTANCE_NAME="$3"
# procServ telnet port
TELNET_PORT="$4"

# env.sh defines APP_NAME and RECIPE_NAME and some other variables
# generated during the recipe build
if [[ ! -f $TOP_DIR/env.sh ]]; then
  echo "env.sh not found in $TOP_DIR folder"
  exit 1
fi
. $TOP_DIR/env.sh

# FIXME: This should come from the env.sh.. or CLI ?!
# specifies any systemd services that need to be started before
# this service
AFTER_SERVICE=

# complete IOC name is composed from the recipe name and the
# instance name
IOC_NAME="${RECIPE_NAME}+${INSTANCE_NAME}"

echo "IOC startup CLI arguments:"
echo " .. mode ${MODE}"
echo " .. name ${IOC_NAME}"
echo " .. folder ${TOP_DIR}"
echo " .. telnet port ${TELNET_PORT}"
echo

function envSet() {
  echo "epicsEnvSet(\"$1\",\"$2\")"
}

# generate envVars (similar to envPaths)
# envVars includes other environment variables than original envPaths does
# envVars does not define all the support module paths at all (all db files are in $(TOP_DIR)/db)
# envVars does not define $(TOP) to avoid warning message at IOC startup (starting the IOC from
# different path path; not build path)
function create_envVars () {
  cat << EOF > $TOP_DIR/ioc/$INSTANCE_NAME/envVars
# created $(date) by $USER @ $(hostname)
$(envSet BUILD_HOST "$BUILD_HOST")
$(envSet BUILD_USER "$BUILD_USER")
$(envSet BUILD_DATETIME "$BUILD_DATETIME")
$(envSet RECIPE_NAME "$RECIPE_NAME")
$(envSet APP "$APP_NAME")
$(envSet IOC_NAME "$IOC_NAME")
$(envSet TOP_DIR "$TOP_DIR")
$(envSet BIN_DIR "$TOP_DIR/bin")
$(envSet DB_DIR "$TOP_DIR/db")
$(envSet DBD_DIR "$TOP_DIR/dbd")
$(envSet IOC_DIR "$TOP_DIR/ioc/$INSTANCE_NAME")
$(envSet AUTOSAVE_DIR "$TOP_DIR/ioc/$INSTANCE_NAME/autosave")
$(envSet LOG_DIR "$TOP_DIR/ioc/$INSTANCE_NAME/log")
$(envSet EPICS_DB_INCLUDE_PATH "$TOP_DIR/db")
$(envSet PATH "$TOP_DIR/bin:$PATH")
EOF
}

# create instance folder if it does not exist yet
if [[ ! -d $TOP_DIR/ioc/$INSTANCE_NAME ]]; then
  mkdir -p $TOP_DIR/ioc/$INSTANCE_NAME || exit 1
fi
# create autosave folder if it does not exist yet
if [[ ! -d $TOP_DIR/ioc/$INSTANCE_NAME/autosave ]]; then
  mkdir -p $TOP_DIR/ioc/$INSTANCE_NAME/autosave || exit 1
fi
# create log folder if it does not exist yet
if [[ ! -d $TOP_DIR/ioc/$INSTANCE_NAME/log ]]; then
  mkdir -p $TOP_DIR/ioc/$INSTANCE_NAME/log || exit 1
fi

# create instance.cmd in the instance folder

if [[ -f $TOP_DIR/ioc/instance.cmd.in && ! -f $TOP_DIR/ioc/$INSTANCE_NAME/instance.cmd ]]; then
  echo "creating $TOP_DIR/ioc/$INSTANCE_NAME/instance.cmd .."
  IFS='
'
  for LINE in $(cat $TOP_DIR/ioc/instance.cmd.in)
  do
    if [[ $(echo "$LINE" | grep '^### MACRO') != "" ]]; then
      MACRO_NAME=$(echo "$LINE" | awk '{print $3;}')
      MACRO_DEFAULT=$(echo "$LINE" | awk '{print $4;}')
      echo -n "specify macro '$MACRO_NAME' value (default '$MACRO_DEFAULT'): "
      read MACRO_VALUE
      [[ -n $MACRO_VALUE ]] || MACRO_VALUE=$MACRO_DEFAULT
      [[ $MACRO_VALUE != "''" ]] || MACRO_VALUE=
      [[ $MACRO_VALUE != "\"\"" ]] || MACRO_VALUE=
      echo "setting macro $MACRO_NAME=$MACRO_VALUE"
      LINE="epicsEnvSet(\"$MACRO_NAME\", \"$MACRO_VALUE\")"
    fi
    echo "$LINE" >> $TOP_DIR/ioc/$INSTANCE_NAME/instance.cmd || exit 1
  done
fi
# copy the main st.cmd to instance folder
echo "updating st.cmd .."
cp $TOP_DIR/ioc/st.cmd.in $TOP_DIR/ioc/$INSTANCE_NAME/st.cmd || exit 1
# copy the dafault_settings*.sav to instance folder
echo "updating default settings .."
for FILE in $TOP_DIR/ioc/default_settings*.sav.in
do
  NAME=$(basename $FILE .sav.in)
  cp $FILE $TOP_DIR/ioc/$INSTANCE_NAME/$NAME.sav || exit 1
done

# generate envVars file in $TOP_DIR/ioc/ folder
create_envVars

# if required create group bde, create user ioc,
# add users $USER and ioc to bde group
grep -q -E "^bde:" /etc/group || {
  echo "creating 'bde' group .."
  sudo groupadd --system bde;
}
grep -q -E "^ioc:" /etc/passwd || {
  echo "creating 'ioc' user .."
  sudo useradd -g bde -G bde \
          -d /var/empty -s `which nologin` \
          -c "BDE user ioc" --system \
          ioc;
}
grep -E "^bde:" /etc/group | grep -q $USER || {
  echo "adding '$USER' user to 'bde' group .."
  sudo gpasswd -a $USER bde;
}
grep -E "^bde:" /etc/group | grep -q ioc || {
  echo "adding 'ioc' user to 'bde' group .."
  sudo gpasswd -a ioc bde;
}

# TODO: would this be OK? Even for dev / debug runs?
# sudo chown -R ioc:bde $ioc
# sudo chmod -R ug+rw $ioc

# handle different startup modes
if [[ $MODE = dev ]]; then
  # execute the application
  pushd $TOP_DIR/ioc/$INSTANCE_NAME >/dev/null
  $TOP_DIR/bin/$APP_NAME st.cmd
  popd >/dev/null

elif [[ $MODE = gdb ]]; then
  pushd $TOP_DIR/ioc/$INSTANCE_NAME >/dev/null
  gdb --args $TOP_DIR/bin/$APP_NAME st.cmd
  popd >/dev/null

elif [[ $MODE = systemd ]]; then
  # generate systemd service file and deploy it
  SERVICE_TEMPLATE=systemd-ioc-template@service
  if [[ ! -f $SERVICE_TEMPLATE ]]; then
    echo "service template $SERVICE_TEMPLATE not found in $TOP_DIR folder"
    exit 1
  fi

  SYSTEMD_DIR=/etc/systemd/system/
  SERVICE_FILE=ioc-${RECIPE_NAME}-${INSTANCE_NAME}.service
  if [[ ! -f $TOP_DIR/$SERVICE_FILE ]]; then
    echo "generating systemd service $SERVICE_FILE .."
    sed \
      -e "s,%TOP_DIR%,$TOP_DIR," \
      -e "s,%INSTANCE_NAME%,$INSTANCE_NAME," \
      -e "s,%APP_NAME%,$APP_NAME," \
      -e "s,%IOC_NAME%,$IOC_NAME," \
      -e "s,%TELNET_PORT%,$TELNET_PORT," \
      -e "s,%AFTER_SERVICE%,$AFTER_SERVICE," \
      $TOP_DIR/$SERVICE_TEMPLATE > $TOP_DIR/$SERVICE_FILE || exit 1
    echo "created $TOP_DIR/$SERVICE_FILE .."
  else
    echo "$TOP_DIR/$SERVICE_FILE already exists .."
  fi
  if [[ ! -f $SYSTEMD_DIR/$SERVICE_FILE ]]; then
    echo "deploying $SERVICE_FILE to $SYSTEMD_DIR .."
    sudo cp $TOP_DIR/$SERVICE_FILE $SYSTEMD_DIR || exit 1
    echo "created $SYSTEMD_DIR/$SERVICE_FILE .."
  else
    echo "$SYSTEMD_DIR/$SERVICE_FILE already exists .."
  fi
  # make sure bde group can write to log and autosave folders
  sudo chgrp -R bde $TOP_DIR/ioc/$INSTANCE_NAME/{log,autosave} || exit 1
  sudo chmod -R g+rw $TOP_DIR/ioc/$INSTANCE_NAME/{log,autosave} || exit 1

  echo "usage:"
  echo " sudo systemctl status $SERVICE_FILE"
  echo " sudo systemctl enable $SERVICE_FILE"
  echo " sudo systemctl start $SERVICE_FILE"
  echo " sudo systemctl stop $SERVICE_FILE"
else
  echo; echo "invalid mode '$MODE'"; echo
  exit 1
fi

echo; echo "SUCCESS"; echo
exit 0
