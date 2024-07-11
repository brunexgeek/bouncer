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

The executable `net-bouncer` will ge created. Use `make install` to install the program in the system or any other location.

## Running

To start the honeypot, run `net-bouncer` specifying the port to listen on using the `-p` parameter. At least one port must be provided. The destination file for the log can be specified with `-l`; if no log file is provided, the output will go to `stderr`.

```sh
$ net-bouncer -p 22 -p 23 -l /var/log/net-bouncer.log
```

The command above will listen on ports 22 (SSH) and 23 (Telnet) and store logs in `/var/log/net-bouncer.log`. The log looks like the following:

```
net-bouncer 0.1.0
2024-07-08 21:26:44.730 [INFO] Listening to any address on the port 22
2024-07-08 21:26:44.730 [INFO] Listening to any address on the port 23
2024-07-08 21:26:48.474 [INFO] Connection from 127.0.0.1 on port 22
2024-07-08 21:26:50.114 [INFO] Connection from 3.3.1.20 on port 23
2024-07-08 21:26:50.738 [INFO] Connection from 64.25.33.120 on port 22
```

The program will generate a log entry for each connection, displaying the remote address and the local port that the remote actor attempted to access.

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

You can use the information from the *net-bouncer*'s log to instruct *fail2ban* to block the IP addresses of machines that triggered the honeypot. I'll assume you already have *fail2ban* installed and operational in your environment. For detailed configuration instructions, refer to the official *fail2ban* documentation.

First, set up a filter that correctly identifies the relevant lines in your log. To do this, create a file named `filter.d/net-bouncer.conf` in the *fail2ban* configuration directory (usually located at `/etc/fail2ban`). Here’s the content for that file:

```
[Definition]
failregex = ^.*Connection from <HOST>.*$
ignoreregex =
datepattern = ^%%Y-%%m-%%d %%H:%%M:%%S
```

Next, we’ll configure the jail settings to associate the filter with the actual log file. Create a file named `jail.d/net-bouncer.conf` in the same configuration directory, adjusting the content to match your specific scenario:

```
[net-bouncer]
enabled = true
logpath = /var/log/net-bouncer.log
bantime = 1w
maxretry = 1
```

If you’re monitoring multiple logs (for example, if you have more than one instance of *net-bouncer*), you can use wildcards (*) in the log path.

Finally, restart the *fail2ban* service:

```sh
$ systemd restart fail2ban.service
```