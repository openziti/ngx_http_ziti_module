`ngx_http_ziti_module`
=====================

Non-blocking upstream module for Nginx to securely connect to a [Ziti Network](https://ziti.dev/about)

<img src="https://ziti.dev/wp-content/uploads/2020/02/ziti.dev_.logo_.png" width="200" />

Learn about Ziti at [ziti.dev](https://ziti.dev)

*This module is not distributed with the Nginx source.* See [the installation instructions](#installation).

<img src="https://ziti-logo.s3.amazonaws.com/ngx.png" width="600" />

Table of Contents
=================

* [Status](#status)
* [Version](#version)
* [Requirements](#requirements)
* [Synopsis](#synopsis)
* [Description](#description)
* [Directives](#directives)
    * [ziti_client_pool](#ziti_client_pool)
    * [ziti_identity](#ziti_identity)
    * [ziti_pass](#ziti_pass)
* [Notes](#notes)
* [Trouble Shooting](#trouble-shooting)
* [Known Issues](#known-issues)
* [Installation](#installation)
* [Compatibility](#compatibility)
* [Report Bugs](#report-bugs)
* [TODO](#todo)

Status
======

This module is currently alpha quality, and should not yet be used in production.

Version
=======

This document describes ngx_http_ziti_module [v0.1.1](https://github.com/openziti/ngx_http_ziti_module/tags).

Requirements
=======

This module requres the `--with-threads` option for `./configure` for compilation.

You'll need to update the config file to match your build environment.

This module assumes the existance of a named thread pool, `ziti`.  To get this module to run, you'll need to add a `thread_pool` directive to your nginx.conf file, e.g.:

```
    thread_pool ziti threads=32 max_queue=65536;
```

If you are using this module, you must figure out how many threads are needed for your particular background operation(s).

Synopsis
========

```nginx

thread_pool ziti threads=32 max_queue=65536;

http {
    ...

    server {
        ...

        location /some_path {
            ziti_pass        my-service-name;
            ziti_identity    /path/to/identity.json;
        }

        ...

    }
}
```

[Back to TOC](#table-of-contents)

Description
===========

This is an nginx upstream module integrating [Ziti](https://ziti.dev) into Nginx in a non-blocking way.

Essentially it provides an efficient and flexible way for nginx internals to reverse-provy to a web server that is dark on the internet and can only be accessed over Ziti.

[Back to TOC](#table-of-contents)

Directives
==========

[Back to TOC](#table-of-contents)

ziti_client_pool_size
-----------------
**syntax:** *ziti_client_pool_size max=&lt;number&gt;;*

**default:** *ziti_client_pool_size max=10*

**context:** *location*

This module supports upstreaming multiple simultaneous HTTP requests to the target Ziti service. Each HTTP request is handled by an internal construct referred to as a `client`. If more HTTP requests arrive than the pool can simultaneously support, some requests will be queued, and will be handled once previous requests complete and the `client` is returned to the pool. When this `client` pool ceiling is hit, you will see a message in the log like the following:

    All available clients [10] are now in use; any additional requests will be queued until clients are returned to pool

You can use this as an indication that you may need to increase your client pool size.

Here's a sample configuration that shows how to adjust the client pool size:

```nginx

    ...

    location /some_path {
        ...
        ziti_client_pool_size   50;     # Increase client pool size to 50
        ...
    }

    ...

```


The following options are supported:

**max=**`<num>`
	Specify the capacity of the client pool for the current location block. The <num> value *must* be greater than zero. This option is default to `10`.

[Back to TOC](#table-of-contents)


ziti_identity
--------------
**syntax:** *ziti_identity &lt;path-to-identity.json&gt;*

**default:** *no*

**context:** *location*

Absoluete path on disk to the Ziti identity file.


```nginx

ziti_identity /some/path/to/identity.json;
```

Note that the name `identity.json` is arbitrary.  The actual file is produced during a separate Ziti Enrollment procedure.

[Back to TOC](#table-of-contents)

ziti_pass
------------
**syntax:** *ziti_pass &lt;service&gt;*

**default:** *no*

**context:** *location, location if*

**phase:** *content*

This directive specifies the Ziti service name to which HTTP requests should be routed in the current location. The `<service>` argument can be any Ziti service name defined within the Ziti network for which the [ziti_identity](#ziti_identity) has access to.

```nginx

ziti_pass my-dark-web-server;
```


[Back to TOC](#table-of-contents)


Notes
=======

During development of this module, one tricky bit was figuring out how to resume processing of the HTTP request after the bacground Ziti operation had completed.  The nginx documentation on [thread pools](http://nginx.org/en/docs/dev/development_guide.html#threads) doesn't cover that topic.  Also, the documentation on [HTTP phases](http://nginx.org/en/docs/dev/development_guide.html#http_phases) erroneously lists `ngx_http_core_run_phases()` as the function to call to resume processing.  Using that doesn't work.  

After digging through the nginx sources, I found the correct function was `ngx_http_handler()`.

Trouble Shooting
================

* to be written...


[Back to TOC](#table-of-contents)

Known Issues
============

* to be written...


[Back to TOC](#table-of-contents)

Installation
============

The installation steps are usually as simple as `./configure --with-threads --add-dynamic-module=../ngx_http_ziti && make && make install` (But you currently still need to install the ziti-sdk-c library manually, see [<https://github.com/openziti/ziti-sdk-c]>(https://github.com/openziti/ziti-sdk-c) for detailed instructions.

...to be written


[Back to TOC](#table-of-contents)

Compatibility
=============

This module has been tested on Linux and Mac OS X. Reports on other POSIX-compliant systems will be highly appreciated.

The following versions of Nginx should work with this module:

* 1.19.x    (last tested: 1.19.7)

Earlier versions of Nginx may *not* work. We haven't tested.


[Back to TOC](#table-of-contents)

Report Bugs
===========

Please submit bug reports, wishlists, or patches by

1. creating a ticket on the [issue tracking interface](http://github.com/openziti/ngx_http_ziti_module/issues) provided by GitHub,

[Back to TOC](#table-of-contents)

TODO
====
* things.

[Back to TOC](#table-of-contents)

