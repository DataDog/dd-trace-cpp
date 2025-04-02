#!/bin/bash
# this script is used to get the container id of the running container
# for reference, see https://github.com/DataDog/datadogpy/blob/master/datadog/dogstatsd/container.py
# include it and call get_container_id() to get the container id, or call it directly,
# e.g "export DD_CONTAINER_ID=$(./container-id.sh)"

set -u

CGROUP_PATH="/proc/self/cgroup"
# CGROUP_MOUNT_PATH="/sys/fs/cgroup"  # cgroup mount path.
CGROUP_NS_PATH="/proc/self/ns/cgroup"  # path to the cgroup namespace file.
# CGROUPV1_BASE_CONTROLLER="memory"  # controller used to identify the container-id in cgroup v1 (memory).
# CGROUPV2_BASE_CONTROLLER=""  # controller used to identify the container-id in cgroup v2.
HOST_CGROUP_NAMESPACE_INODE=4026531835  # 0xEFFFFFFB  # inode of the host cgroup namespace.
MOUNTINFO_PATH="/proc/self/mountinfo"  # path to the mountinfo file.

LINE_RE="^([0-9]+):([^:]*):(.+)$"  # regex to parse the cgroup file.

UUID_SOURCE="[0-9a-f]{8}[-_][0-9a-f]{4}[-_][0-9a-f]{4}[-_][0-9a-f]{4}[-_][0-9a-f]{12}"
CONTAINER_SOURCE="[0-9a-f]{64}"
TASK_SOURCE="[0-9a-f]{32}-[0-9]+"
CONTAINER_RE="(.+)?($UUID_SOURCE|$CONTAINER_SOURCE|$TASK_SOURCE)(\.scope)?$"  # regex to match the container id.

# define method to check if the current process is in a host cgroup namespace.
is_host_cgroup_namespace()
{
    # check if the cgroup namespace file exists.
    if [ -f "$CGROUP_NS_PATH" ]; then

        # get the inode of the cgroup namespace file.
        inode=$(stat -Lc %i "$CGROUP_NS_PATH")
        
        # check if the inode of the cgroup namespace file is the same as the inode of the host cgroup namespace.
        if [ "$inode" -eq "$HOST_CGROUP_NAMESPACE_INODE" ]; then
            return 0
        fi
    fi
    return 1
}

get_cgroup_inode()
{
    # get the inode of the cgroup namespace file.
    inode=$(stat -Lc %i "$CGROUP_NS_PATH")
    echo "$inode"
}

# define method to read the container id from the cgroup file
read_cgroup_path()
{
    # find all lines that match the regex.
    while IFS= read -r line; do
        #echo $line
        # strip leading and trailing whitespace.
        line=$(echo "$line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
        if [[ "$line" =~ $LINE_RE ]]; then
            # get the controller and the path.
            # controller="${BASH_REMATCH[2]}"
            path="${BASH_REMATCH[3]}"
            #echo path $path

            # split $path by / and iterate over the parts
            IFS='/' read -r -a path_parts <<< "$path"
            for part in "${path_parts[@]}"; do
                # match the container id from path
                #echo part $part
                if [[ "$part" =~ $CONTAINER_RE ]]; then
                    echo "${BASH_REMATCH[2]}"
                    return 0
                fi
            done
        fi
    done < "$CGROUP_PATH"
    return 1
}

#define method to read container id from the mountinfo file.
# this is needed for cgroup v2, where the cgroup file is empty.
# the lines look like this:
#647 646 0:55 /docker/47e6bf8be66c1a5206309fffa130784a157d42bb4d8bc4151646430a437d22c8 /sys/fs/cgroup/cpuset ro,nosuid,nodev,noexec,relatime master:130 - cgroup cpuset rw,cpuset
# and we want to extract the container id from the path whic is the fourth field in the file
# see https://stackoverflow.com/questions/68816329/how-to-get-docker-container-id-from-within-the-container-with-cgroup-v2
read_mountinfo()
{
    # iterate over lines in mountinfo
    while IFS= read -r line; do
        #echo $line
        # strip leading and trailing whitespace.
        line=$(echo "$line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
        # split the line by space
        IFS=' ' read -r -a parts <<< "$line"
        # get the path from the fourth field
        path="${parts[3]}"
        #split the path into parts
        IFS='/' read -r -a path_parts <<< "$path"
        # iterate over the parts
        for part in "${path_parts[@]}"; do
            # match the container id from path
            if [[ "$part" =~ $CONTAINER_RE ]]; then
                echo "${BASH_REMATCH[2]}"
                return 0
            fi
        done
    done < "$MOUNTINFO_PATH"
}

get_container_id()
{
    # statsd use either the proper container id (64 characters) or the cgroup inode number as
    # an idntifier, "ci-<container-id>"" or "in-<cgroup-inode>"" respectively.
    if is_host_cgroup_namespace; then
        container_id=$(read_cgroup_path)
        status=$?
        if [ $status -ne 0 ]; then
            echo "Failed reading container id from cgroup" >&2
            exit $status
        fi
        echo "ci-$container_id"
        exit 0
    fi
    inode=$(get_cgroup_inode)
    echo "in-$inode"
    exit 0
}
