80-update-htop-and-offload-tx
=============================

[armbian](https://www.armbian.com) is a Debian based Linux distro
for virtually every ARM dev board. As it is slim, easy to use and configure
it is one of my favorite distros for my ARM-boards zoo.

Yet, there is a LPE vulnerability in Armbian's *NetworkManager* dispatcher script
`80-update-htop-and-offload-tx`.

The script goes like this:

```bash
#!/bin/bash
#
# adjust htop settings for all normal users and root
#

. /etc/armbian-release

for homedir in $(awk -F'[:]' '{if ($3 >= 1000 && $3 != 65534 || $3 == 0) print $6}' /etc/passwd); do

	unset FIELDS FIELD_PARA

	# start with a clean copy
	cp /etc/skel/.config/htop/htoprc "${homedir}"/.config/htop/htoprc

	for TYPE in ethernet wifi; do

		i=0
		for INTERFAC in $(LC_ALL=C nmcli device status | grep $TYPE | grep -w connected | awk '{ print $1 }' | grep -v lo | grep -v p2p | head -2); do

			[[ $TYPE == ethernet ]] && type="eth"; [[ $TYPE == wifi ]] && type="wlan"

			FIELDS+="${type^}$i ${type^}${i}stat "
			FIELD_PARA+="2 2 "
			sed -i "s/^${type}${i}_alias=.*/${type}${i}_alias=$INTERFAC/" "${homedir}"/.config/htop/htoprc

			((i=i+1))
		done
	done

	FIELDS=$(echo $FIELDS | xargs)
	FIELD_PARA=$(echo $FIELD_PARA | xargs)

	echo "$FIELDS $FIELD_PARA"

	sed -i "s/right_meters.*$/& $FIELDS/" "${homedir}"/.config/htop/htoprc
	sed -i "s/right_meter_modes.*$/& $FIELD_PARA/" "${homedir}"/.config/htop/htoprc

	# enable GPU where this works
	if [[ $LINUXFAMILY == meson64 || $LINUXFAMILY == odroidxu4 ]]; then
	        sed -i "s/left_meters.*$/& GpuTemp/" "${homedir}"/.config/htop/htoprc
        	sed -i "s/left_meter_modes.*$/& 2/" "${homedir}"/.config/htop/htoprc
	fi

done
```

The interesting part is that while the dispatcher script is run as root when a network device is
coming up, it will operate on files in every users home directory. We will exploit the call to `sed`.
Let me repeat that the vulnerability is not within *sed*, but we are using it as a vector since the
sequence of syscalls allow us to drop arbitrary content to (almost) arbitrary files. We aim to
create content inside `/etc/sudoers.d/` that allows us to *sudo* to root without password.

Here's a strace of a *sed* call:
```
5248  [b6e6bbe6] openat(AT_FDCWD, ".config/htop/htoprc", O_RDONLY|O_LARGEFILE) = 3
[...]
5248  [b6e6bbe6] openat(AT_FDCWD, ".config/htop/sedlMbD4Z", O_RDWR|O_CREAT|O_EXCL|O_LARGEFILE, 0600) = 4
5248  [b6e6bbe6] read(3, "# Beware! This file is rewritten"..., 4096) = 967
5248  [b6e6bbe6] fstat64(4, {st_mode=S_IFREG|000, st_size=0, ...}) = 0
5248  [b6e6bbe6] read(3, "", 4096)      = 0
[...]
5248  [b6e6bbe6] close(3)               = 0
5248  [b6e6bbe6] write(4, "# Beware! This file is rewritten"..., 981) = 981
5248  [b6e6bbe6] close(4)               = 0
5248  [b6e6bbe6] rename(".config/htop/sedlMbD4Z", ".config/htop/htoprc") = 0
```

The sed invocation will edit the file in place and creates a temporary file which is then renamed
to `$HOME/.config/htop/htoprc`. The exploit will switch the `.config/htop` directory after the first
`openat()` to point to `/etc/sudoers.d`. This will land controlled content into a root owned file
that is accepted by *sudo*.

Here is a sample run:

![screenshot](https://github.com/stealth/7350topless/blob/master/screenshot.jpg)

The vulnerable dispatcher script will be called when DHCP rebind time is reached, so you either setup
a hostile DHCP server who offers short rebind time or wait until it triggers, which can take a couple
of hours as most DHCP servers offer DHCP leases with ~6h of rebind time.

There are some nifty details about the directory switch which you can enjoy by reading the
exploit code. As the PoC potentially waits quite long, we will try to reduce CPU usage. In
other circumstances, due to multi-core environments, spinning would be an option.

