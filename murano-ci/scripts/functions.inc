#!/bin/bash
#
# Common functions file
#
function trap_handler() {
    cat << EOF
********************************************************************************
*
*   Got error in "'$3'", line "'$2'", error code "'$1'"
*
********************************************************************************
EOF
}

function exit_handler() {
    mkdir -p $WORKSPACE/artifacts
    if [[ "${1}" -ne 0 ]]; then
        tear_down
    fi
    echo "$0: ${BUILD_STATUS_ON_EXIT}" >> $WORKSPACE/artifacts/build_status
    echo "$0: ${BUILD_STATUS_ON_EXIT}" >> $WORKSPACE/artifacts/overall-status.txt
    cat << EOF
********************************************************************************
*
*   Exiting script, exit code "'$1'"
*   Build status: $BUILD_STATUS_ON_EXIT
*
********************************************************************************
EOF
    set +o xtrace
    while [ -f ~/keep-vm-alive ]; do
        sleep 5
    done
}

function lowercase() {
    local input=$1
    local retval="$(echo $input | sed -e 's/\(.*\)/\L\1/')"
    echo "$retval"
}

function set_config() {
    local section=$1
    local option=$2
    local value=$3
    local file=$4
    local line

    if [ -z "$section" ] ; then
        # No section name specified
        sed -i -e "s/^\($option[ \t]*=[ \t]*\).*$/\1$value/" "$file"
    else
        # Check if section already exists
        if ! grep -q "^\[$section\]" "$file" ; then
            # Add section at the end
            echo -e "\n[$section]" >>"$file"
        fi

        # Check if parameter in the section exists
        line=$(sed -ne "/^\[$section\]/,/^\[.*\]/ { /^$option[ \t]*=/ p; }" "$file")
        if [ -z "$line" ] ; then
            # Add parameter if it is not exists
            sed -i -e "/^\[$section\]/ a\\
$option = $value
" "$file"
        else
            # Replace existing parameter
            sed -i -e "/^\[$section\]/,/^\[.*\]/ s|^\($option[ \t]*=[ \t]*\).*$|\1$value|" "$file"
        fi
    fi
}

function get_os() {
    local retval=0
    local OS=$(uname)

    if [[ -z "${OS}" ]]; then
        if [ "${OS}" = "Linux" ] ; then
            if [ -f /etc/redhat-release ] ; then
                DISTRO_BASED_ON='RedHat'
            elif [ -f /etc/SuSE-release ] ; then
                DISTRO_BASED_ON='SuSe'
            elif [ -f /etc/debian_version ] ; then
                DISTRO_BASED_ON='Debian'
            fi
            DISTRO_BASED_ON=$(lowercase $DISTRO_BASED_ON)
            readonly DISTRO_BASED_ON
        else
            echo "Unsupported operating system:\"$OS\", sorry!"
            retval=1
        fi
    else
        retval=0
    fi
    return $retval
}

function shield_slashes() {
    local input=$1
    local retval="${input//\/\\}"
    echo "$retval"
}

function get_ip_from_iface() {
    local iface_name=$1

    if [[ -z "${FOUND_IP_ADDRESS}" ]]; then
        FOUND_IP_ADDRESS="$(ifconfig $iface_name | awk -F ' *|:' '/inet addr/{print $4}')"

        if [ $? -ne 0 ] || [ -z "$FOUND_IP_ADDRESS" ]; then
            echo "Can't obtain ip address from interface $iface_name!"
            return 1
        else
            readonly FOUND_IP_ADDRESS
        fi
    fi
}

function get_floating_ip() {
    if [[ -z "${FLOATING_IP_ADDRESS}" ]]; then
        sudo apt-get update && sudo apt-get -y install python-virtualenv
        local tmp_venv=$(mktemp -d)
        virtualenv "${tmp_venv}"
        "${tmp_venv}/bin/pip" install python-openstackclient

        export OS_USERNAME=${ADMIN_USERNAME}
        export OS_PASSWORD=${ADMIN_PASSWORD}
        export OS_TENANT_NAME=${ADMIN_TENANT}
        export OS_PROJECT_NAME=${ADMIN_TENANT}
        if [[ ${ZUUL_BRANCH} =~ liberty ]]; then
            export OS_AUTH_URL="http://${OPENSTACK_HOST}:5000/v2.0"
        else
            export OS_AUTH_URL="http://${OPENSTACK_HOST}:5000/v3"
            export OS_IDENTITY_API_VERSION=3
        fi
        export OS_DEFAULT_DOMAIN='default'
        export OS_USER_DOMAIN_NAME='Default'
        export OS_PROJECT_DOMAIN_NAME='Default'

        FLOATING_IP_ADDRESS=$("${tmp_venv}/bin/openstack" floating ip list | grep " ${FOUND_IP_ADDRESS} " | cut -d ' ' -f 4)

        if [ -z "${FLOATING_IP_ADDRESS}" ]; then
            echo "Unable to find floating IP"
            echo "Nova Client output:"
            "${tmp_venv}/bin/openstack" floating ip list
            exit 1
        fi

        rm -rf ${tmp_venv}

        readonly FLOATING_IP_ADDRESS
    fi
}

function clone_project() {
    local project_root=$1
    git clone "${ZUUL_URL}/${ZUUL_PROJECT}" "${project_root}"
    pushd "${project_root}"
    git fetch "${ZUUL_URL}/${ZUUL_PROJECT}" "${ZUUL_REF}" && git checkout FETCH_HEAD
    popd
}

function get_commit_message() {
    local project_root=$1
    pushd "${project_root}"
    git log --format=%B -n 1
    popd
}

function get_dependencies() {
    local commit_message=$1
    IFS=$'\n'
    for dependency in $(echo -e "${commit_message}" | grep -o "^Depends-On: .*$"); do
        dependency=$(echo "${dependency}" | awk '{ print $2 }')
        echo "${dependency}"
    done
}

function get_dependency_info() {
    local dependency=$1

    local metadata=$(curl "https://review.openstack.org/changes/${dependency}/detail?o=CURRENT_REVISION" 2> /dev/null)
    local ref=$(echo -e "${metadata}" | grep -o '"ref": ".*$' | awk -F'"' '{ print $4 }')
    local project=$(echo -e "${metadata}" | grep -o '"project": ".*,' | awk -F'"' '{ print $4 }')

    echo "${project}:${ref}"
}

function override_refference() {
    local tmp_dir=$(mktemp -d)

    clone_project "${tmp_dir}"
    local msg=$(get_commit_message ${tmp_dir})

    for dependency in $(get_dependencies "${msg}"); do
        local dep_info=$(get_dependency_info "${dependency}")
        local dep_project=$(echo "${dep_info}" | awk -F':' '{ print $1 }')
        local dep_ref=$(echo "${dep_info}" | awk -F':' '{ print $2 }')

        case ${dep_project} in
            "openstack/murano")
                MURANO_BRANCH=${dep_ref}
            ;;
            "openstack/murano-dashboard")
                MURANO_DASHBOARD_BRANCH=${dep_ref}
            ;;
            "openstack/murano-agent")
                DIB_MURANO_AGENT_REF=${dep_ref}
            ;;
            "openstack/python-muranoclient")
                MURANO_PYTHONCLIENT_BRANCH=${dep_ref}
                LIBS_FROM_GIT='python-muranoclient'
            ;;
            *)
                echo "Unsupported dependent project: ${dep_project}"
            ;;
        esac
    done

    rm -rf "${tmp_dir}"
}

function remove_image_with_murano_agent() {

    for i in $(glance image-list | get_field 2); do
        if [ "${DISTR_NAME}_${BUILD_NUMBER}" == "$i" ]; then
            glance image-delete "$i"
        fi
    done
}

function save_image_with_murano_agent() {

    for i in $(glance image-list | get_field 2); do
        if [ "${DISTR_NAME}_latest" == "$i" ]; then
            glance image-delete "$i"
        fi
    done

    sleep 20

    for i in $(glance image-list | get_field 2); do
        if [ "${DISTR_NAME}_${BUILD_NUMBER}" == "$i" ]; then
            glance image-update --name "${DISTR_NAME}_latest" \
                --property 'murano_image_info'="{\"type\": \"linux\", \"title\": \"${DISTR_NAME}_latest\"}" "$i"
        fi
    done
}

function ensure_no_heat_stacks_left() {
    local log_file="${STACK_HOME}/log/murano-engine.log"
    local retval=0

    for stack_name in $(grep -oP '(?<=generatedStackName\", \").*?(?=\"]+, )' "${log_file}" | sort | uniq); do
        # get string, containing stack id
        stack_id_string=$(openstack stack list | grep "${stack_name}" | cut -d '|' -f 2)
        # strip spaces
        stack_id="${stack_id_string//[[:blank:]]/}"
        if [ -n "${stack_id}" ]; then
             retval=1
             stack_info=$(openstack stack list | grep "${stack_id}")
             set +o xtrace
             cat << EOF
********************************************************************************
*
*   Stack $stack_id found!
*   Full stack information:
*   $stack_info
*   Deleting stack $stack_id
*
********************************************************************************
EOF
             set -o xtrace
             openstack stack delete "${stack_id}" --yes > /dev/null
        fi
    done

    return $retval
}

function tear_down() {

    if [[ "${PROJECT_NAME}" == 'murano-agent' ]]; then
        remove_image_with_murano_agent
    fi
}
