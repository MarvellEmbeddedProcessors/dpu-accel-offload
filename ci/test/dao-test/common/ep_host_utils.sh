#!/bin/bash
# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2024 Marvell.

HOST_UTILS_SCRIPT_PATH=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
source "$HOST_UTILS_SCRIPT_PATH/ep_common_ops.sh"

HOST_DEPENDENCIES="iperf iperf3"

function ep_host_pre_setup()
{
	#Install required packages
	apt-get install -y $HOST_DEPENDENCIES
}

function ep_host_sdp_setup()
{
	set +e # Module may be already loaded
	if [[ -n ${EP_HOST_MODULE_DIR:-} ]]; then
		insmod $EP_HOST_MODULE_DIR/octeon_ep.ko
	else
		insmod $EP_DIR/ep_files/octeon_ep.ko
	fi
	set -e
	sleep 5
}

function ep_host_vdpa_common_setup()
{
	local driver=$1
	local part=$2
	local host_pf
	local vf_cnt
	local vf_cnt_max
	local sdp_vfs

	echo "Setting up VDPA on host"
	modprobe vdpa
	if [[ "$driver" == "vhost_vdpa" ]]; then
		echo "Loading vhost-vdpa module"
		modprobe vhost-vdpa
	fi
	if [[ "$driver" == "virtio_vdpa" ]]; then
		echo "Loading virtio-vdpa module"
		modprobe virtio-vdpa
	fi
	set +e # Module may be already loaded
	rmmod octep_vdpa
	if [[ -n ${EP_HOST_MODULE_DIR:-} ]]; then
		insmod $EP_HOST_MODULE_DIR/octep_vdpa.ko
	else
		insmod $EP_DIR/kmod/vdpa/octeon_ep/octep_vdpa.ko
	fi
	set -e

	host_pf=$(ep_common_pcie_addr_get ${part}00)
	vf_cnt=1
	vf_cnt_max=$(cat /sys/bus/pci/devices/$host_pf/sriov_totalvfs)
	vf_cnt=$((vf_cnt >vf_cnt_max ? vf_cnt_max : vf_cnt))

	ep_common_bind_driver pci $host_pf octep_vdpa
	ep_common_set_numvfs $host_pf $vf_cnt

	sleep 1
	# Get the list of management devices
	mgmt_devices=$(vdpa mgmtdev show | awk '/pci\/0000:/{print $1}' | sed 's/:$//')
	for mgmtdev in $mgmt_devices; do
		vdpa_name="vdpa${mgmtdev##*/}"
		vdpa dev add name "$vdpa_name" mgmtdev "$mgmtdev"
		sleep 1
	done

	set +x
	sdp_vfs=$(ep_common_pcie_addr_get ${part}03)
	for dev in $sdp_vfs; do
		set +e # Grep can return non-zero status
		vdev=$(ls /sys/bus/pci/devices/$dev | grep vdpa)
		while [[ "$vdev" == "" ]]; do
			echo "Waiting for vdpa device for $dev"
			sleep 1
			vdev=$(ls /sys/bus/pci/devices/$dev | grep vdpa)
		done
		set -e

		echo "Binding $vdev to $driver"
		ep_common_bind_driver vdpa $vdev $driver
	done
}

function ep_host_vdpa_setup()
{
	ep_host_vdpa_common_setup vhost_vdpa $1
}

function ep_host_virtio_vdpa_setup()
{
	local host_netdev_ip_addr=$2
	ep_host_vdpa_common_setup virtio_vdpa $1
	# Once device is bound to virtio-vdpa without any issues,
	# linux netdev will be created
	sleep 2
	set +e # grep can return non-zero status
	if [[ -d /sys/class/net ]]; then
		for dev in /sys/class/net/*; do
			if [[ $(readlink $dev/device/driver | grep virtio_net) ]]; then
				ifconfig $(basename $dev) $host_netdev_ip_addr
			fi
		done
	fi
	set -e
}

function ep_host_vdpa_cleanup()
{
	echo "Cleaning up VDPA on host"
	set +e # Module may be already loaded
	rmmod octep_vdpa
	rmmod vhost-vdpa
	rmmod vdpa
	set -e
}

function ep_host_virtio_vdpa_cleanup()
{
	echo "Cleaning up VIRTIO-VDPA on host"
	set +e # Module may be already loaded
	for vdev in $(ls /sys/bus/vdpa/devices/);
	do
		if [[ $(readlink /sys/bus/vdpa/devices/$vdev/driver | grep virtio_vdpa) ]]; then
			echo $vdev > /sys/bus/vdpa/drivers/virtio_vdpa/unbind
		fi
	done
	rmmod octep_vdpa
	rmmod virtio-vdpa
	rmmod vdpa
	set -e
}

EP_SCP_CMD=${EP_SCP_CMD:-"scp -o LogLevel=ERROR -o ServerAliveInterval=30 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"}
EP_GUEST_DIR="/root/hostshare"
EP_GUEST_SHARE_DIR=$EP_DIR/guest

function ep_host_launch_guest()
{
	local unbuffer="stdbuf -o0"
	local pfx=$1
	local in=guest.in.$pfx
	local out=guest.out.$pfx

	$EP_SCP_CMD ci@10.28.36.188:/home/ci/dao/host/qemu-system-x86_64 $EP_DIR/
	$EP_SCP_CMD ci@10.28.36.188:/home/ci/dao/host/noble-server-cloudimg-amd64.img $EP_DIR/
	$EP_SCP_CMD ci@10.28.36.188:/home/ci/dao/host/bios-256k.bin /usr/share/qemu
	$EP_SCP_CMD ci@10.28.36.188:/home/ci/dao/host/vgabios-stdvga.bin /usr/share/qemu
	$EP_SCP_CMD ci@10.28.36.188:/home/ci/dao/host/efi-virtio.rom /usr/share/qemu

	# Folder to be shared with the guest
	rm -rf $EP_GUEST_SHARE_DIR
	mkdir $EP_GUEST_SHARE_DIR

	cp $EP_DIR/ci/test/dao-test/common/utils.sh $EP_GUEST_SHARE_DIR
	cp $EP_DIR/ci/test/dao-test/common/testpmd.sh $EP_GUEST_SHARE_DIR
	cp $EP_DIR/ci/test/dao-test/common/ep_guest_utils.sh $EP_GUEST_SHARE_DIR

	if [[ -f $EP_DIR/qemu-system-x86_64 ]]; then
		QEMU_BIN=$EP_DIR/qemu-system-x86_64
	else
		echo "qemu-system-x86_64 not found !!"
		return 1
	fi

	if [[ -f $EP_DIR/noble-server-cloudimg-amd64.img ]]; then
		VM_IMAGE=$EP_DIR/noble-server-cloudimg-amd64.img
	else
		echo "x86 QEMU cloud image not found !!"
		return 1
	fi

	ulimit -l unlimited
	rm -f $out
	rm -f $in
	touch $in
	tail -f $in | ($unbuffer $QEMU_BIN -hda "$VM_IMAGE" -name vm1  \
	-netdev type=vhost-vdpa,vhostdev="/dev/vhost-vdpa-0",id=vhost-vdpa1 \
	-device e1000,netdev=netdev0,romfile=/usr/share/qemu/efi-virtio.rom \
	-netdev user,id=netdev0,hostfwd=tcp::2222-:22 \
	-device virtio-net-pci,netdev=vhost-vdpa1,disable-modern=off,page-per-vq=on,packed=on,mrg_rxbuf=on,mq=on,rss=on,rx_queue_size=1024,tx_queue_size=1024,disable-legacy=on -fsdev local,path=$EP_GUEST_SHARE_DIR,security_model=passthrough,id=hostshare -device virtio-9p-pci,id=fs0,fsdev=hostshare,mount_tag=host_tag \
	-enable-kvm -nographic -m 2G -cpu host -smp 8 -L /usr/share/qemu &>$out) &

	# Wait for guest to be up
	local itr=0
	while ! (tail -n15 $out | grep -q "ubuntu login:"); do
		sleep 10
		itr=$((itr + 1))
		if [[ itr -eq 20 ]]; then
			echo "Timeout waiting for Guest";
			cat $out
			return 1;
		fi
		echo "Waiting for guest to be up"
	done
	echo "Guest is launched"
	echo "root" >>$in
	sleep 1;
	echo "a" >>$in
	sleep 1;
	echo "rm -rf $EP_GUEST_DIR; mkdir $EP_GUEST_DIR" >>$in
	echo "cp /home/dpdk-testpmd /bin" >> $in
	echo "mount -t 9p -o trans=virtio host_tag $EP_GUEST_DIR" >>$in
	echo "$EP_GUEST_DIR/ep_guest_utils.sh setup" >> $in
	echo "cd $EP_GUEST_DIR" >> $in
}

function guest_testpmd_prompt()
{
	local pfx=$1
	local refresh=${2:-}
	local skip_bytes=${3:-}
	local in=$EP_GUEST_SHARE_DIR/testpmd.in.$pfx
	local out=$EP_GUEST_SHARE_DIR/testpmd.out.$pfx

	local cmd="tail -n1 $out"

	if [[ "$skip_bytes" != "" ]]
	then
		cmd="tail -c +$skip_bytes $out"
	fi

	while ! ($cmd | grep -q "^testpmd> $"); do
		if [ "$refresh" == "yes" ]
		then
			sleep 1
			echo "" >>$in
		fi
		continue;
	done
}

function ep_host_start_guest_traffic()
{
	local unbuffer="stdbuf -o0"
	local pfx=$1
	local in=guest.in.$pfx
	local out=guest.out.$pfx
	local testpmd_out="$EP_GUEST_SHARE_DIR/testpmd.out.$pfx"
	local args=${@:2}

	echo "Starting Traffic on Guest"
	echo "./ep_guest_utils.sh testpmd_launch $pfx $args" >> $in
	# Wait till out file is created
	local itr=0
	while [[ ! -f $testpmd_out ]]; do
		itr=$((itr + 1))
		sleep 1
		if [[ itr -eq 20 ]]; then
			echo "Timeout waiting for Guest testpmd";
			cat $out
			return 1;
		fi
		echo "Waiting for guest testpmd to be up"
		continue
	done
	# Wait till testpmd prompt comes up
	guest_testpmd_prompt $pfx
	echo "./ep_guest_utils.sh testpmd_start $pfx" >> $in
	echo "Started Traffic on Guest"
}

function ep_host_guest_testpmd_pps()
{
	local pfx=$1
	local in=guest.in.$pfx
	local testpmd_pps="$EP_GUEST_SHARE_DIR/testpmd.pps.$pfx"
	local rx_pps

	echo "./ep_guest_utils.sh testpmd_pps $pfx" >> $in
	while [[ ! -f $testpmd_pps ]]; do
		sleep 1
		echo "Waiting for $testpmd_pps to be created"
	done
	rx_pps=$(cat $testpmd_pps)
	if [[ $rx_pps -eq 0 ]]; then
		echo "Low PPS for ${pfx} ($rx_pps == 0)"
		return 1
	else
		echo "Rx PPS $rx_pps as expected"
		return 0
	fi
}

function ep_host_stop_guest_traffic()
{
	local pfx=$1
	local in=guest.in.$pfx
	local testpmd_out=$EP_GUEST_SHARE_DIR/testpmd.out.$pfx

	cat $testpmd_out
	echo "./ep_guest_utils.sh testpmd_stop $pfx" >> $in
}

function ep_host_netdev_config()
{
	local pfx=$1
	local in=guest.in.$pfx
	local out=guest.out.$pfx
	local args=${@:2}

	echo "Configuring guest netdev"
	echo "./ep_guest_utils.sh netdev_config $pfx $args" >> $in
}

function ep_host_netdev_ping_test()
{
	local pfx=$1
	local in=guest.in.$pfx
	local out=guest.out.$pfx
	local args=${@:2}
	local ping_test_fail_res="$EP_GUEST_SHARE_DIR/netdev.ping_fail.out.$pfx"
	local ping_test_pass_res="$EP_GUEST_SHARE_DIR/netdev.ping_pass.out.$pfx"
	local res

	echo "./ep_guest_utils.sh start_ping_test $pfx $args" >> $in
	# Wait ping command to complete
	sleep 60
	if [[ -f $ping_test_fail_res ]]; then
		echo "Ping test failed"
		echo "$(cat $ping_test_fail_res)"
		return 1
	elif [[ -f $ping_test_pass_res ]]; then
		echo "$(cat $ping_test_pass_res)"
		return 0
	fi
}

function ep_host_shutdown_guest()
{
	local pfx=$1
	local in=guest.in.$pfx

	echo "cd /home" >>$in
	echo "umount $EP_GUEST_DIR" >>$in
	echo "shutdown now" >>$in
	sleep 10;
}

function ep_host_sdp_vf_setup()
{
	local sdp_pf=$1
	local num_vf=$2
	local sdp_vfs

	set +e # Module may be already loaded
	if [[ -n ${EP_HOST_MODULE_DIR:-} ]]; then
		insmod $EP_HOST_MODULE_DIR/octeon_ep_vf.ko
	else
		insmod $EP_DIR/ep_files/octeon_ep_vf.ko
	fi
	set -e
	sleep 2

	max_vfs=$(cat /sys/bus/pci/devices/$sdp_pf/sriov_totalvfs)
	num_vf=$((num_vf >  max_vfs ? max_vfs : num_vf))

	echo 0 > /sys/bus/pci/devices/$sdp_pf/sriov_numvfs
	sleep 3
	echo $num_vf > /sys/bus/pci/devices/$sdp_pf/sriov_numvfs
	sleep 10

	echo $(ep_common_pcie_addr_get "0xB903" all)
}

function ep_host_sdp_vf_cleanup()
{
	local sdp_pf=$1

	echo 0 > /sys/bus/pci/devices/$sdp_pf/sriov_numvfs
	rmmod octeon_ep_vf
}

function ep_host_sdp_vfs_ip_cnf()
{
	local arg1=("$@")
	local ip="${!#}"
	local sdp_vfs=${arg1[@]:0:${#arg1[@]}-1}

	IFS='.' read -r -a ip_parts <<< "$ip"
	for vf in $sdp_vfs; do
		vf_ip="${ip_parts[0]}.${ip_parts[1]}.${ip_parts[2]}.${ip_parts[3]}"
		ep_common_if_configure --pcie-addr $vf --ip $vf_ip
		((ip_parts[3]++))
	done
}


# If this script is directly invoked from the shell execute the
# op specified
if [[ ${BASH_SOURCE[0]} == ${0} ]]; then
	OP=$1
	ARGS=${@:2}
	if [[ $(type -t ep_host_$OP) == function ]]; then
		ep_host_$OP $ARGS
	elif [[ $(type -t ep_common_$OP) == function ]]; then
		ep_common_$OP $ARGS
	else
		$OP $ARGS
	fi
fi
