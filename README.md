# net-bouncer

This is a honeypot program that logs connection attempts to a specified port (any address) and then disconnects. No data is received or sent.

The idea is to use it in conjunction with [fail2ban](https://github.com/fail2ban/fail2ban) to block IP addresses attempting to exploit known services, such as SSH, while the authentic service is offered on a non-standard port. This way, you can prevent malicious or unknown actors from having any type of communication with your services.

## Build

To build the software, all you need is a C99-compatible compiler and `make`.

First, clone the repository using `git` or download the source code in the [project's GitHub page](https://github.com/brunexgeek/net-bouncer):

```sh
$ git clone https://github.com/brunexgeek/net-bouncer.git
```

Go to the source directory and run `make`:

```sh
$ cd net-bouncer
$ make
```

The executable `net-counver` will ge created. Use `make install` to install the program in the system or any other location.

## Running

To run `net-bouncer`, run the program indicating the port to listen on. The port is specified with the `-p` parameter and the destination file for the log is specified with `-l`. If no log file is specified, the output go to `stderr`.

```sh
$ net-counter -p 22 -l /var/log/net-bouncer-22.log
```

The command above will listen on port 22 (SSH) and store logs in `/var/log/net-bouncer-22.log`. The log looks like the following:

```
2024-07-08 21:26:44.730 [INFO] Listening at port 22
2024-07-08 21:26:48.474 [INFO] Connection from 127.0.0.1
2024-07-08 21:26:50.114 [INFO] Connection from 3.3.1.20
2024-07-08 21:26:50.738 [INFO] Connection from 64.25.33.120
```

If you want to honeypot more than one port, launch one instance for each port.

## Running as service with systemd

The best way to run *net-bouncer* is using *systemd*. You can use a service description like the following:

```
[Unit]
Wants=network-online.target
After=network-online.target

[Service]
User=net-bouncer
Group=net-bouncer
ExecStart=net-bouncer -p 22 -l /var/log/net-bouncer-22.log

[Install]
WantedBy=default.target
```

The service above assumes you have a user and group named `net-bouncer`, which is the recomended thing to do. If you don't want to create a specific user to run `net-bouncer`, you can omit the fields `User` and `Group`.

## Monitoring the log with fail2ban

You can use the information contained in the *net-bouncer*'s log to let *fail2ban* block the addresses of the machines that fell for the honeypot. I'm assuming you have fail2ban installed and working in your environment. For details about the configuration, check the official *fail2ban* documentation.

First you need a filter that will match the correct lines in the log. Create the file `filter.d/net-bouncer.conf` in the *fail2ban* configuration directory (usually `/etc/fail2ban`) with the following content:

```
[Definition]
failregex = ^.*Connection from <HOST>.*$
ignoreregex =
datepattern = ^%%Y-%%m-%%d %%H:%%M:%%S
```

Now we need to add the jail configuration with associate the filter and the actual log file. Create the file `jail.d/net-bouncer.conf` in the *fail2ban* configuration directory with the following content (adjust it for your scenario):

```
[net-bouncer]
enabled = true
logpath = /var/log/net-bouncer.log
bantime = 1w
maxretry = 1
```

If you have multiple logs to watch (i.e. more than one instance of *net-bouncer*), you can use wildcards (*) in the log path.

Now just restart *fail2ban* service:

```sh
$ systemd restart fail2ban.service
```