/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Ericsson AB
 */

#ifndef XCM_H
#define XCM_H
#ifdef __cplusplus
extern "C" {
#endif

/*! @mainpage Extensible Connection-oriented Messaging
 *
 * @tableofcontents
 *
 * @section introduction Introduction
 *
 * This is the documentation for the Extensible Connection-oriented
 * Messaging (XCM) programming APIs.
 *
 * XCM consists the core API in xcm.h, an address helper library API
 * in xcm_addr.h, and the attribute APIs in xcm_attr.h and
 * xcm_attr_map.h. Obsolete, but still present, functions are
 * available in xcm_compat.h
 *
 * @author Mattias Rönnblom
 * @version 0.15 [API]
 * @version 1.1.0 [Implementation]
 *
 * The low API/ABI version number is purely a result of all XCM
 * releases being backward compatible, and thus left the major version
 * at 0. It's not to be taken as a sign of immaturity. The API and its
 * implementation have been stable for years.
 *
 * @section overview Overview
 *
 * XCM provides a connection-oriented, reliable messaging service with
 * in-order delivery. The design goal is to allow for a straight
 * forward mapping to TCP and TLS, but also supporting efficient
 * inter-process commmunication (IPC) mechanisms for local
 * communication.
 *
 * XCM reuses much of the terminology of the BSD Sockets API. Unlike
 * the BSD Socket API, XCM has uniform semantics across all underlying
 * transports.
 *
 * @section semantics Overall Service Semantics
 *
 * XCM has a client-server model. A server creates a server socket
 * (with xcm_server()) bound to a specific address (in case of TCP or
 * TLS, a TCP port on a particular IP interface), after which clients
 * may initiate connections to the server.  On a successful attempt,
 * two connection sockets will be created; one on the server side
 * (returned from xcm_accept()), and one of the client side (returned
 * from xcm_connect()). Thus, a server serving multiple clients will
 * have multiple sockets; one server socket and N connection sockets,
 * one each for every client. A client will typically have one
 * connection socket for each server it is connected to.
 *
 * Messages are always sent and received on a particular connection
 * socket (and never on a server socket).
 *
 * @subsection ordering Ordering Guarantees
 *
 * In-order delivery - that messages arrive at the recipient in the
 * same order they were sent by the sender side - is guaranteed, but
 * only for messages sent on the same connection.
 *
 * @subsection flow_control Flow Control
 *
 * XCM transports support flow control. Thus, if the sender message
 * rate or bandwidth is higher than the network or the receiver can
 * handle on a particular connection, xcm_send() in the sender process
 * will eventually block (or return an error EAGAIN, if in
 * non-blocking mode). Unless XCM is used for bulk data transfer (as
 * oppose to signaling traffic), xcm_send() blocking because of slow
 * network or a slow receiver should be rare indeed in practice. TCP,
 * TLS, and UNIX domain socket transports all have large enough
 * windows and socket buffers to allow a large amount of outstanding
 * data.
 *
 * @section addressing Addressing and Transport Selection
 *
 * In XCM, the application is in control of which transport will be
 * used, with the address supplied to xcm_connect() and xcm_server()
 * including both the transport name and the transport address.
 *
 * However, there is nothing preventing a XCM transport to use a more
 * an abstract addressing format, and internally include multiple
 * "physical" IPC transport options. This model is used by the @ref
 * utls_transport.
 *
 * @subsection address_syntax Address Syntax
 *
 * Addresses are represented as
 * strings with the following general syntax:
 * <tt><transport-name>:<transport-address></tt>
 *
 * For the UX UNIX Domain Socket transport, the addresses has this
 * more specific form: @n
 * @code ux:<UNIX domain socket name> @endcode
 *
 * The addresses of the UXF UNIX Domain Socket transport variant
 * have the following format: @n
 * @code uxf:<file system path> @endcode
 *
 * For the TCP, TLS, UTLS and SCTP transports the syntax is: @n
 * @code
 * tcp:(<DNS domain name>|<IPv4 address>|[<IPv6 address>]|[*]|*):<port>
 * tls:(<DNS domain name>|<IPv4 address>|[<IPv6 address>]|[*]|*):<port>
 * utls:(<DNS domain name>|<IPv4 address>|[<IPv6 address>]|[*]|*):<port>
 * sctp:(<DNS domain name>|<IPv4 address>|[<IPv6 address>]|[*]|*):<port>
 * @endcode
 *
 * '*' is a shorthand for '0.0.0.0' (i.e. bind to all IPv4
 * interfaces).  '[*]' is the IPv6 equivalent, creating a server
 * socket accepting connections on all IPv4 and IPv6 addresses.
 *
 * Some examples addresses:
 * @code
 * tcp:*:4711
 * tls:192.168.1.42:4711
 * tcp:[::1]:99
 * tcp:[*]:4711
 * tls:service:4711
 * sctp:service.company.com:42
 * @endcode
 *
 * For TCP, TLS, UTLS and SCTP server socket addresses, the port can
 * be set to 0, in which case XCM (or rather, the Linux kernel) will
 * allocate a free TCP port from the local port range.
 *
 * @subsubsection dns DNS Resolution
 *
 * For transports allowing a DNS domain name as a part of the address,
 * the transport will attempt resoĺv the name to an IP address. A DNS
 * domain name may resolv to zero or more IPv4 addresses and/or zero
 * or more IPv6 addresses. XCM relies on the operating system to
 * prioritize between IPv4 and IPv6.
 *
 * @subsubsection ip_addr_format IPv4 Address Format
 *
 * XCM accepts IPv4 addresses in the dotted-decimal format
 * @code
 * 130.236.254.2
 * @endcode
 *
 * XCM allows only complete addresses with three '.', and not the
 * archaic, classful, forms, where some bytes where left out, and thus
 * the address contained fewer separators.
 *
 * @section dpd Dead Peer Detection
 *
 * XCM transports attempt to detect a number of conditions which can
 * lead to lost connectivity, and does so even on idle connections.
 *
 * If the remote end closes the connection, the local xcm_receive()
 * will return 0. If the process on the remote end crashed,
 * xcm_receive() will return -1 and set errno ECONNRESET. If network
 * connectivity to the remote end is lost, xcm_receive() will return
 * -1 and errno will be set to ETIMEDOUT.
 *
 * @section error_handling Error Handling
 *
 * In general, XCM follow the UNIX system API tradition when it comes
 * to error handling. Where possible, errors are signaled to the
 * application by using unused parts of the value range of the
 * function return type. For functions returning signed integer types,
 * this means the value of -1 (in case -1 is not a valid return
 * value). For functions returning pointers, NULL is used to signal
 * that an error has occurred. For functions where neither -1 or NULL
 * can be used, or where the function does not return anything
 * (side-effect only functions), an 'int' is used as the return type,
 * and is used purely for the purpose to signal success (value 0), or
 * an error (-1) to the application.
 *
 * The actual error code is stored in the thread-local errno
 * variable. The error codes are those from the fixed set of errno
 * values defined by POSIX, found in errno.h. Standard functions such
 * as perror() and strerror() may be used to turn the code into a
 * human-readable string.
 *
 * In non-blocking operation, given the fact the actual transmission
 * might be defered (and the message buffered in the XCM layer), and
 * that message receive processing might happen before the application
 * has called receive, the error being signaled at the point of a
 * certain XCM call might not be a direct result of the requested
 * operation, but rather an error discovered previously.
 *
 * The documentation for xcm_finish() includes a list of generic error
 * codes, applicable xcm_connect(), xcm_accept(), xcm_send() and
 * xcm_receive().
 *
 * Also, for errors resulting in an unusable connection, repeated
 * calls will produce the same errno.
 *
 * @section select Event-driven Programming Support
 *
 * In UNIX-style event-driven programming, a single application thread
 * handles multiple clients (and thus multiple XCM connection sockets)
 * and the task of accepting new clients on the XCM server socket
 * concurrently (although not in parallel). To wait for events from
 * multiple sources, an I/O multiplexing facility such as select(2),
 * poll(2) or epoll(2) is used.
 *
 * Each XCM socket is represented by a single fd, retrieved with
 * xcm_fd(). The fd number and underlying file object is stable across
 * the life-time of the socket.
 *
 * On BSD Sockets, the socket fd being readable means it's likely that
 * the application can successfully read data from the
 * socket. Similarily, a fd marked writable by, for example, poll()
 * means that the application is likely to be able to write data to
 * the BSD Sockets fd. For an application using XCM going into
 * select(), it must @a always wait for all the fds its XCM sockets to
 * become readable (e.g. being in the @p readfds in the select()
 * call), regardless what are their target conditions. Thus, even if
 * the application is waiting for an opportunity to try to send a
 * message on a XCM socket, or it doesn't want to do anything with the
 * socket, it must wait for the socket @a fd to become readable. Not
 * wanting to do nothing here means that the application has the
 * xcm_await() @p condition set to 0, and is neither interested in
 * waiting to call xcm_send(), xcm_receive(), nor xcm_accept() on the
 * socket. An application may never leave a XCM socket unattended in
 * the sense its fd is not in the set of fds passed to select() and/or
 * xcm_send(), xcm_receive(), xcm_accept() or xcm_finish() are not
 * called.
 *
 * @subsection select_variants Supported I/O Multiplexing Facilities
 *
 * XCM is oblivious to what I/O multiplexing mechanism employed by the
 * application. It may call select(), poll() or epoll_wait() directly,
 * or make use of any of the many available event loop libraries (such
 * as libevent). For simplicity, select() is used in this
 * documentation to denote the whole family of Linux I/O multiplexing
 * facilities.
 *
 * @subsection non_blocking_ops Non-blocking Operation
 *
 * An event-driven application needs to set the XCM sockets it handles
 * into non-blocking mode, by calling xcm_set_blocking(), setting the
 * "xcm.blocking" socket attribute, or using the XCM_NONBLOCK flag in
 * xcm_connect().
 *
 * For XCM sockets in non-blocking mode, all potentially blocking API
 * calls related to XCM connections - xcm_connect(), xcm_accept(),
 * xcm_send(), and xcm_receive() - finish immediately.
 *
 * For xcm_send(), xcm_connect() and xcm_accept(), XCM signaling
 * success means that the XCM layer has accepted the request. It may
 * or may not have completed the operation.
 *
 * @subsubsection non_blocking_connect Non-blocking Connection Establishment
 *
 * In case the @ref XCM_NONBLOCK flag is set in the xcm_connect()
 * call, or in case the a XCM server socket is in non-blocking mode at
 * the time of a xcm_accept() call, the newly created XCM connection
 * returned to the application may be in a semi-operational state,
 * with some internal processing and/or signaling with the remote peer
 * still required before actual message transmission and reception may
 * occur.
 *
 * The application may attempt to send or receive messages on such
 * semi-operational connections.
 *
 * There are ways for an application to determine when connection
 * establishment or the task of accepting a new client have
 * completed. See @ref outstanding_tasks for more information.
 *
 * @subsubsection non_blocking_send_receive Non-blocking Send and Receive
 *
 * To receive a message on a XCM connection socket in non-blocking
 * mode, the application may need to wait for the right conditions to
 * arise (i.e. a message being available). The application needs to
 * inform the socket that it wants to receive by calling xcm_await()
 * with the @p XCM_SO_RECEIVABLE bit in the @p condition bit mask set.
 * It will pass the fd it received from xcm_fd() into select(), asking
 * to get notified when the fd becomes readable. When select() marks
 * the socket fd as readable, the application should issue
 * xcm_receive() to attempt to retrieve a message.
 *
 * xcm_receive() may also called on speculation, prior to any select()
 * call, to poll the socket for incoming messages.
 *
 * A XCM connection socket may have a number of messages buffered, and
 * applications should generally, for optimal performance, repeat
 * xcm_receive() until it returns an error, and errno is set to
 * EAGAIN.
 *
 * Similarly to receiving a message, an application may set the @p
 * XCM_SO_SENDABLE bit in the @p condition bit mask, if it wants to
 * wait for a socket state where it's likely it can successfully send
 * a message. When select() marks the socket fd as @a readable, the
 * application should attempt to send a message.
 *
 * Just like with xcm_receive(), it may also choose to issue a
 * xcm_send() call on speculation (i.e. without going into select()),
 * which is often a good idea for performance reasons.
 *
 * For send operations on non-blocking connection sockets, XCM may
 * buffer whole or part of the message before transmission to the
 * lower layer. This may be due to socket output buffer underrun, or
 * the need for some in-band signaling, like cryptographic key
 * exchange, to happen before the transmission of the complete message
 * may finish. The XCM layer will (re-)attempt to hand the message
 * over to the lower layer at a future call to xcm_finish(),
 * xcm_send(), or xcm_receive().
 *
 * For applications wishing to determine when all buffered messages
 * have successfully be deliver to the lower layer, they may use
 * xcm_finish() to do so. Normally, applications aren't expected to
 * require this kind of control. Please also note that the fact a
 * message has left the XCM layer doesn't necessarily mean it has
 * successfully been delivered to the recipient.
 *
 * @subsubsection outstanding_tasks Finishing Outstanding Tasks
 *
 * xcm_connect(), xcm_accept(), xcm_send() may all leave the socket in
 * a state where work is initiated, but not completed. In addition,
 * the socket may have pending internal tasks, such flushing the
 * output buffer into the TCP/IP stack, processing XCM control
 * interface messages, or finishing the TLS hand shake procedure.
 *
 * After waking up from a select() call, where a particular XCM
 * non-blocking socket's fd is marked readable, the application must,
 * if no xcm_send(), xcm_receive() or xcm_accept() calls are to be
 * made, call xcm_finish(). This is to allow the socket to finish any
 * outstanding tasks, even in the case the application has no
 * immediate plans for the socket.
 *
 * Prior to changing a socket from non-blocking to blocking mode, any
 * outstanding tasks should be finished, or otherwise the switch might
 * cause xcm_set_blocking() to return -1 and set errno to EAGAIN.
 *
 * @subsection might_block Ready Status Semantics
 *
 * For example, if a server socket's desired condition has been set
 * (with xcm_await()) to @p XCM_SO_ACCEPTABLE, and the application
 * wakes up from select() with the socket's fd marked readable, a call
 * to xcm_accept() may still not produce a new connection socket.
 *
 * The same holds true when reaching @p XCM_SO_RECEIVABLE and a
 * xcm_receive() call is made, and @p XCM_SO_SENDABLE and calls to
 * xcm_send().
 *
 * @subsection nb_examples Non-blocking Example Sequences
 *
 * @subsubsection nb_connect_and_send Connect and Send Message
 *
 * In this example, the application connects and tries to send a
 * message, before knowing if the connection is actually
 * established. This may fail (for example, in case TCP and/or
 * TLS-level connection establishement has not yet been completed), in
 * which case the application will fall back and wait with the use of
 * xcm_await(), xcm_fd() and select().
 *
 * @startuml{nb_connect_and_send.png}
 * client -> libxcm: xcm_connect("tls:192.168.1.42:4711", XCM_NONBLOCK);
 * libxcm -> client: conn_socket
 * client -> libxcm: xcm_send(conn_socket, "hello world", 11);
 * libxcm -> client: -1, errno=EAGAIN
 * client -> libxcm: xcm_fd(conn_socket);
 * libxcm -> client: 42
 * client -> libxcm: xcm_await(conn_socket, XCM_SO_SENDABLE);
 * libxcm -> client: 0
 * client -> libc: select(17, [42, ...], [...], [...], NULL);
 * |||
 * libc -> client: 1
 * client -> libxcm: xcm_send(conn_socket, "hello world", 11);
 * libxcm -> client: -1, errno=EAGAIN
 * client -> libc: select(17, [42, ...], [...], [...], NULL);
 * |||
 * libc -> client: 1
 * client -> libxcm: xcm_send(conn_socket, "hello world", 11);
 * libxcm -> client: 0
 * @enduml
 *
 * @subsubsection nb_connect_explicit Connect with Explicit Finish
 *
 * In case the application wants to know when the connection
 * establishment has finished, it may use xcm_finish() to do so, like
 * in the below example sequence.
 *
 * @startuml{nb_connect_explicit.png}
 * client -> libxcm: xcm_connect("tls:192.168.1.42:4711", XCM_NONBLOCK);
 * libxcm -> client: conn_socket
 * client -> libxcm: xcm_fd(conn_socket);
 * libxcm -> client: 99
 * client -> libxcm: xcm_await(conn_socket, 0);
 * libxcm -> client: 0
 * client -> libc: select(88, [99, ...], [...], [...], NULL);
 * |||
 * libc -> client: 1
 * client -> libxcm: xcm_finish(conn_socket);
 * libxcm -> client: -1, errno=EAGAIN
 * client -> libc: select(88, [...], [99, ...], [...], NULL);
 * |||
 * libc -> client: 1
 * client -> libxcm: xcm_finish(conn_socket);
 * libxcm -> client: -1, errno=EAGAIN
 * client -> libc: select(88, [...], [99, ...], [...], NULL);
 * |||
 * libc -> client: 1
 * client -> libxcm: xcm_finish(conn_socket);
 * libxcm -> client: 0
 * @enduml
 *
 * @subsubsection nb_immediate_connection_refused Immediate Connection Refused
 *
 * While connecting to a server socket, the client's connection
 * attempt may be refused immediately.
 *
 * @startuml{nb_immediate_connection_refused.png}
 * client -> libxcm: xcm_connect("utls:192.168.1.17:17", XCM_NONBLOCK);
 * libxcm -> client: NULL, errno=ECONNREFUSED
 * @enduml
 *
 * @subsubsection nb_delayed_connection_refused Delayed Connection Refused
 *
 * In many cases, the application is handed a connection socket before
 * the connection establishment is completed. Any errors occuring
 * during this process is handed over to the application at the next
 * XCM call; would it be xcm_finish(), xcm_send() or xcm_receive().
 *
 * @startuml{nb_delayed_connection_refused.png}
 * client -> libxcm: xcm_connect("utls:192.168.1.17:17", XCM_NONBLOCK);
 * libxcm -> client: conn_socket
 * client -> libxcm: xcm_fd(conn_socket);
 * libxcm -> client: 100
 * client -> libxcm: xcm_await(conn_socket, XCM_SO_SENDABLE);
 * libxcm -> client: 0
 * client -> libc: select(50, [100, ...], [...], [...], NULL);
 * |||
 * libc -> client: 1
 * client -> libxcm: xcm_send(conn_socket, "Greetings from the North", 25);
 * libxcm -> client: -1, errno=ECONNREFUSED
 * client -> libxcm: xcm_close(conn_socket);
 * libxcm -> client: 0
 * @enduml
 *
 * @subsubsection nb_flush_buffers_before_close Buffer Flush Before Close
 *
 * In this example the application flushes any internal XCM buffers
 * before shutting down the connection, to ensure that any buffered
 * messages are delivered to the lower layer.
 *
 * @startuml{nb_flush_buffers_before_close.png}
 * client -> libxcm: xcm_send(conn_socket, msg, 100);
 * libxcm -> client: 0
 * client -> libxcm: xcm_finish(conn_socket);
 * libxcm -> client: -1, errno=EAGAIN
 * client -> libxcm: xcm_fd(conn_socket);
 * libxcm -> client: 12
 * client -> libxcm: xcm_await(conn_socket, 0);
 * libxcm -> client: 0
 * client -> libc: select(13, [12, ...], [...], [...], NULL);
 * |||
 * libc -> client: 1
 * client -> libxcm: xcm_finish(conn_socket);
 * libxcm -> client : 0
 * client -> libxcm: xcm_close(conn_socket);
 * libxcm -> client : 0
 * @enduml
 *
 * @subsubsection nb_server_accept Server Accept
 *
 * In this sequence, a server accepts a new connection, and continues
 * to attempt to receive a message on this connection, while still,
 * concurrently, is ready to accept more clients on the server socket.
 *
 * @startuml{server_accept.png}
 * client -> libxcm: xcm_server("tcp:*:17");
 * libxcm -> client: server_socket
 * client -> libxcm: xcm_set_blocking(server_socket, false);
 * libxcm -> client: 0
 * client -> libxcm: xcm_fd(server_socket);
 * libxcm -> client: 4
 * client -> libxcm: xcm_await(server_socket, XCM_SO_ACCEPTABLE);
 * libxcm -> client: 0
 * client -> libc: select(3, [4, ...], [...], [...], NULL);
 * |||
 * libc -> client: 1
 * client -> libxcm: xcm_accept(server_socket);
 * libxcm -> client: conn_socket
 * client -> libxcm: xcm_fd(conn_socket);
 * libxcm -> client: 5
 * client -> libxcm: xcm_await(conn_socket, XCM_SO_RECEIVABLE);
 * libxcm -> client: 0
 * client -> libc: select(3, [4, 5, ...], [...], [...], NULL);
 * |||
 * libc -> client: 1
 * client -> client: map_active_fd_to_xcm_socket()
 * client -> libxcm: xcm_receive(conn_socket, buf, 1024);
 * libxcm -> client: 100
 * client -> client: handle_request(buf, 100);
 * @enduml
 *
 * @section attributes Socket Attributes
 *
 * Tied to an XCM server or connection socket is a set of key-value
 * pairs known as attributes. Which attributes are available varies
 * across different transports, and different socket types.
 *
 * An attribute's name is a string, and follows a hierarchical naming
 * schema. For example, all generic XCM attributes, available in all
 * transports, have the prefix "xcm.". Transport-specific attributes
 * are prefixed with the transport or protocol name (e.g. "tcp." for
 * TCP-specific attributes applicable to the TLS and TCP transports).
 *
 * An attribute may be read-only, write-only or available both for
 * reading and writing. This is referred to as the attribute's mode.
 * The mode may vary across the lifetime of the socket. For example,
 * an attribute may be writable at the time of the xcm_connect()
 * call, and read-only thereafter.
 *
 * The attribute value is coded in the native C data type and byte
 * order. Strings are NUL-terminated, and the NUL character is
 * included in the length of the attribute. There are four value
 * types; a boolean type, a 64-bit signed integer type, a string type
 * and a type for arbitrary binary data. See xcm_attr_types.h for
 * details.
 *
 * The attribute access API is in xcm_attr.h.
 *
 * Retrieving an integer attribute's value may look like this:
 * ~~~~~~~~~~~~~{.c}
 * int64_t rtt;
 * xcm_attr_get(tcp_conn_socket, "tcp.rtt", NULL, &rtt, sizeof(rtt));
 * printf("Current TCP round-trip time estimate is %ld us.", rtt);
 * ~~~~~~~~~~~~~
 *
 * Changing an integer attribyte value may be done in the following manner:
 * ~~~~~~~~~~~~~{.c}
 * int64_t interval = 10;
 * xcm_attr_set(tcp_conn_socket, "tcp.keepalive_interval", xcm_attr_type_int64, &interval, sizeof(interval));
 * ~~~~~~~~~~~~~
 *
 * Both of these examples are missing error handling.
 *
 * @subsubsection attr_map Attribute Maps
 *
 * XCM allows supplying a set of writable attributes at the time of
 * socket creation, by using the xcm_connect_a(),
 * xcm_server_a(), or xcm_accept_a() functions.
 *
 * The attribute sets are represented by the @ref xcm_attr_map type in
 * xcm_attr_map.h.
 *
 * An example:
 * ~~~~~~~~~~~~~{.c}
 * struct xcm_attr_map *attrs = xcm_attr_map_create();
 * xcm_attr_map_add_bool(attrs, "xcm.blocking", false);
 * xcm_attr_map_add_str(attrs, "xcm.local_addr", "tls:192.168.1.42:0");
 * xcm_attr_map_add_int64(attrs, "tcp.keepalive_interval", 10);
 *
 * int rc = xcm_connect_a("tls:192.168.1.99:4711", attrs);
 *
 * xcm_attr_map_destroy(attrs);
 * ~~~~~~~~~~~~~
 *
 * @subsection xcm_attr Generic Attributes
 *
 * These attributes are expected to be found on XCM sockets regardless
 * of transport type.
 *
 * For TCP transport-specific attributes, see @ref
 * tcp_attr, and for TLS, see @ref tls_attr.
 *
 * Attribute Name | Socket Type | Value Type | Mode | Description
 * ---------------|-------------|------------|------|------------
 * xcm.type       | All         | String     | R    | The socket type: "server" or "connection".
 * xcm.transport  | All         | String     | R    | The transport type.
 * xcm.local_addr | All         | String     | RW   | The local address of a socket. Writable only if supplied to xcm_connect_a().
 * xcm.blocking   | All         | Boolean    | RW    | See xcm_set_blocking() and xcm_is_blocking().
 * xcm.remote_addr | Connection | String     | R    | See xcm_remote_addr().
 * xcm.max_msg_size | Connection | Integer   | R    | The maximum size of any message transported by this connection.
 *
 * @subsubsection cnt_attr Generic Message Counter Attributes
 *
 * XCM has a set of generic message counters, which keeps track of the
 * number of messages crossing a certain boundary for a particular
 * connection, and a sum of their size.
 *
 * Some of the message and byte counter attributes use the concept of
 * a "lower layer". What this means depends on the transport. For the
 * UX And TCP transports, it is the Linux kernel. For example, for
 * TCP, if the xcm.to_lower_msgs is incremented, it means that XCM has
 * successfully sent the complete message to the kernel's networking
 * stack for further processing. It does not means it has reached the
 * receiving process. It may have, but it also may be sitting on the
 * local or remote socket buffer, on a NIC queue, or be in-transmit in
 * the network. For TLS, the lower layer is OpenSSL.
 *
 * All the "xcm.*_bytes" counters count the length of the XCM message
 * payload (as in the length field in xcm_send()), and thus does not
 * include any underlying headers.
 *
 * The message counters only count messages succesfully sent and/or
 * received.
 *
 * Attribute Name       | Socket Type | Value Type | Mode | Description
 * ---------------------|-------- ----|------------|------|------------
 * xcm.from_app_msgs    | Connection  | Integer    | R    | Messages sent from the application and accepted into XCM.
 * xcm.from_app_bytes   | Connection  | Integer    | R    | The sum of the size of all messages counted by xcm.from_app_msgs.
 * xcm.to_app_msgs      | Connection  | Integer    | R    | Messages delivered from XCM to the application.
 * xcm.to_app_bytes     | Connection  | Integer    | R    | The sum of the size of all messages counter by xcm.to_app_msgs.
 * xcm.from_lower_msgs  | Connection  | Integer    | R    | Messages received by XCM from the lower layer.
 * xcm.from_lower_bytes | Connection  | Integer    | R    | The sum of the size of all messages counted by xcm.from_lower_msgs.
 * xcm.to_lower_msgs    | Connection  | Integer    | R    | Messages successfully sent by XCM into the lower layer.
 * xcm.to_lower_bytes   | Connection  | Integer    | R    | The sum of the size of all messages counted by xcm.to_lower_msgs.
 *
 * @section ctl Control Interface
 *
 * XCM includes a control interface, which allows iteration over the
 * OS instance's XCM server and connection sockets (for processes with
 * the appropriate permissions), and access to their attributes (see
 * @ref attributes).
 *
 * The control interface is optional by means of build-time
 * configuration.
 *
 * For each XCM server or connection socket, there is a corresponding
 * UNIX domain socket which is used for control signaling (i.e. state
 * retrieval).
 *
 * @subsection ctl_dir Control UNIX Socket Directory
 *
 * By default, the control interface's UNIX domain sockets are stored in
 * the @c /run/xcm/ctl directory.
 *
 * This directory needs to be created prior to running any XCM
 * applications (for the control interface to worker properly) and
 * should be writable for all XCM users.
 *
 * A particular process using XCM may be configured to use a
 * non-default directory for storing the UNIX domain sockets used for
 * the control interface by means of setting the @c XCM_CTL
 * variable. Please note that using this setting will cause the XCM
 * connections to be not visible globally on the OS instance (unless
 * all other XCM-using processes also are using this non-default
 * directory).
 *
 * @subsection ctl_errors Control Interface Error Handling
 *
 * Generally, since the application is left unaware (from an API
 * perspective) of the existence of the control interface, errors are
 * not reported up to the application. They are however logged.
 *
 * Application threads owning XCM sockets, but which are busy with
 * non-XCM processing for a long duration of time, or otherwise are
 * leaving their XCM sockets unattended to (in violation of XCM API
 * contract), will not respond on the control interface's UNIX domain
 * sockets (corresponding to their XCM sockets). Only the presence of
 * these sockets may be detected, but their state cannot be retrieved.
 *
 * @subsection ctl_api Control API
 *
 * Internally, the XCM implementation has control interface client
 * library, but this library's API is not public at this point.
 *
 * @subsection ctl_shell Command-line Control Program
 *
 * XCM includes a command-line program @c xcmctl which uses the @ref
 * ctl_api to iterate of the system's current XCM sockets, and allow
 * access (primarily for debugging purposes) to the sockets'
 * attributes.
 * 
 * @section thread_safety Thread Safety
 *
 * Unlike BSD sockets, a XCM socket may not be shared among different
 * threads without synchronization external to XCM. With proper
 * external serialization, a socket may be shared by different threads
 * in the same process, although it might provide difficult in
 * practice since a thread in a blocking XCM function will continue to
 * hold the lock, and thus preventing other threads from accessing the
 * socket at all.
 *
 * For non-blocking sockets, threads sharing a socket need to agree on
 * what is the appropriate socket @p condition to wait for. When this
 * condition is met, all threads are woken up, returning from
 * select().
 *
 * It is safe to "give away" a XCM socket from one thread to another,
 * provided the appropriate memory fences are used.
 *
 * These limitations (compared to BSD Sockets) are in place to allow
 * socket state outside the kernel (which is required for TCP framing
 * and TLS).
 *
 * @section fork Multi-processing and Fork
 *
 * Sharing a XCM socket between threads in different processes is not
 * possible.
 *
 * After a fork() call, either of the two process (the parent, or the
 * child) must be designated the owner of every XCM socket the parent
 * owned.
 *
 * The owner may continue to use the XCM socket normally.
 *
 * The non-owner may not call any other XCM API call than
 * xcm_cleanup(), which frees local memory tied to this socket
 * in the non-owner's process address space, without impacting the
 * connection state in the owner process.
 *
 * @section transports Transports
 *
 * The core XCM API functions are oblivious to the transports
 * used. However, the support for building, and parsing addresses are
 * available only for a set of pre-defined set of transports. There is
 * nothing preventing xcm_addr.h from being extended, and also nothing
 * prevents an alternative XCM implementation to include more
 * transports without extending the address helper API.
 *
 * @subsection ux_transport UX Transport
 *
 * The UX transport uses UNIX Domain (AF_UNIX, also known as AF_LOCAL)
 * Sockets.
 *
 * UX sockets may only be used with the same OS instance (or, more
 * specifically, between processes in the same Linux kernel network
 * namespace).
 *
 * UNIX Domain Sockets comes in a number of flavors, and XCM uses the
 * SOCK_SEQPACKET variety. SOCK_SEQPACKET sockets are
 * connection-oriented, preserves message boundaries and delivers
 * messages in the same order they were sent; perfectly matching XCM
 * semantics and provides for an near-trivial mapping.
 *
 * UX is the most efficient of the XCM transports.
 *
 * @subsubsection ux_naming UX Namespace
 *
 * The standard UNIX Domain Sockets as defined by POSIX uses the file
 * system as its namespace, with the sockets also being
 * files. However, for simplicity and to avoid situations where stale
 * socket files (originating from crashed processes) causing problems,
 * the UX transport uses a Linux-specific extension, allowing a
 * private UNIX Domain Socket namespace. This is known as the abstract
 * namespace (see the unix(7) man page for details). With the abstract
 * namespace, server socket address allocation has the same life time
 * as TCP ports (i.e. if the process dies, the address is free'd).
 *
 * The UX transport enables the SO_PASSCRED BSD socket option, to give
 * the remote peer a name (which UNIX domain connection socket doesn't
 * have by default). This is for debugging and observability purposes.
 * Without a remote peer name, in server processes with multiple
 * incoming connections to the same server socket, it's difficult to
 * say which of the server-side connection sockets goes to which
 * remote peer. The kernel-generated, unique, name is an integer in
 * the form "%05x" (printf format). Applications using hardcoded UX
 * addresses should avoid such names by, for example, using a prefix.
 *
 * The @ref utls_transport also indirectly uses the UX namespace, so
 * care should be taken to avoid any clashes between UX and UTLS
 * sockets in the same network namespace.
 *
 * @subsection uxf_transport UXF Transport
 *
 * The UXF transport is identical to the UX transport, only it uses
 * the standard POSIX naming mechanism. The name of a server socket 
 * is a file system path, and the socket is also a file.
 *
 * The UXF sockets resides in a file system namespace, as opposed to
 * UX sockets, which live in a network namespace.
 *
 * Upon xcm_close(), the socket will be closed and the file removed.
 * If an application crashes or otherwise fails to run xcm_close(), it
 * will leave a file in the file system pointing toward a non-existing
 * socket. This file will prevent the creation another server socket
 * with the same name.
 *
 * @subsection tcp_transport TCP Transport
 *
 * The TCP transport uses the Transmission Control Protocol (TCP), by
 * means of the BSD Sockets API.
 *
 * TCP is a byte-stream service, but the XCM TCP transport adds
 * framing on top of the stream. A single-field 32-bit header
 * containing the message length in network byte order is added to
 * every message.
 *
 * TCP uses TCP Keepalive to detect lost network connectivity between
 * the peers.
 *
 * The TCP transport supports IPv4 and IPv6.
 *
 * Since XCM is designed for signaling traffic, the TCP transport
 * disables the Nagle algorithm of TCP to avoid its excessive latency.
 *
 * @subsubsection tcp_attr TCP Socket Attributes
 *
 * The read-only TCP attributes are retrieved from the kernel (struct
 * tcp_info in linux/tcp.h).
 *
 * The read-write attributes are mapped directly to setsockopt() calls.
 *
 * See the tcp(7) manual page for a more detailed description of these
 * attributes. The struct retrieved with @c TCP_INFO is the basis for
 * the read-only attributes. The read-write attributes are mapped to
 * @c TCP_KEEP* and @c TCP_USER_TIMEOUT.
 *
 * Attribute Name     | Socket Type | Value Type | Mode | Description
 * -------------------|-------------|------------|------|------------
 * tcp.rtt            | Connection  | Integer    | R    | The current TCP round-trip estimate (in us).
 * tcp.total_retrans  | Connection | Integer  | R    | The total number of retransmitted TCP segments.
 * tcp.segs_in        | Connection  | Integer    | R    | The total number of segments received.
 * tcp.segs_out       | Connection  | Integer    | R    | The total number of segments sent.
 * tcp.keepalive      | Connection  | Boolean    | RW   | Controls if TCP keepalive is enabled.
 * tcp.keepalive_time | Connection  | Integer    | RW   | The time (in s) before the first keepalive probe is sent on an idle connection.
 * tcp.keepalive_interval | Connection | Integer | RW   | The time (in s) between keepalive probes.
 * tcp.keepalive_count | Connection | Integer    | RW   | The number of keepalive probes sent before the connection is dropped.
 * tcp.user_timeout   | Connection  | Integer    | RW   | The time (in s) before a connection is dropped due to unacknowledged data.
 *
 * @warning @c tcp.segs_in and @c tcp.segs_out are only present when
 * running XCM on Linux kernel 4.2 or later.
 *
 * @subsection tls_transport TLS Transport
 *
 * The TLS transport uses TLS to provide a secure, private, two-way
 * authenticated transport.
 *
 * TLS is a byte-stream service, but the XCM TLS transport adds
 * framing in the same manner as does the XCM TCP transport.
 *
 * The TLS transport supports IPv4 and IPv6.
 *
 * The TLS transport disables the Nagle algorithm of TCP.
 *
 * @subsubsection tls_certificates TLS Certificate and Key Storage
 *
 * The TLS transport expect the certificate, trust chain and private
 * key files to be found in a file system directory - the certificate
 * directory. The default path are configured at build-time, but can
 * be overriden on a per-process basis by means of a UNIX environment
 * variable. The current value of @c XCM_TLS_CERT (at the time of
 * xcm_connect() or xcm_accept()) determines the certificate directory
 * used for that connection.
 *
 * In case the files in the certificate directory are modified, the
 * new versions of the files will be used by new connections. The TLS
 * transports works with differences between set of files, and thus
 * the new generation of files need not nesserarily be newer
 * (i.e. higher mtime).
 * 
 * The certificate directory must be updated in an atomic manner, or
 * XCM may end up using the certificate file from one generation of
 * files and the key file from another. One way of achieving an atomic
 * update is to have the certificate directory being a symbolic link
 * to the directory where the actual files are located. Upon update, a
 * new directory is created and populated, and the old symbolic link
 * is replace with a new one in an atomic manner (i.e. with
 * rename(2)). It's legal to switch back-and-forth between two sets of
 * files, but the time between the back and the forth switch (assuming
 * the atomicity-by-symlink method is used) must be enough to result
 * in different file system modification times on the symbolic link
 * (usually ~10 ms).
 *
 * The TLS transport will, at the time of XCM socket creation
 * (xcm_connect() or xcm_server()), look up the process' current
 * network namespace. In case the namespace is given a name per the
 * iproute2 methods and conventions, XCM will retrieve this name and
 * use it in the certificate and key lookup.
 *
 * In the certificate directory, the TLS transport expects the
 * certificate to follow the below naming convention (where <ns>
 * is the namespace):
 * @code
 * cert_<ns>.pem
 * @endcode
 *
 * The private key is stored in:
 * @code
 * key_<ns>.pem
 * @endcode
 *
 * The trust chain is stored in:
 * @code
 * tc_<ns>.pem
 * @endcode
 *
 * For the default namespace (or any other network namespace not named
 * according to iproute2 standards), the certificate need to be stored
 * in a file "cert.pem", the private key in "key.pem" and the trust
 * chain in "tc.pem".
 *
 * In case the certificate, key or trust chain files are not in place
 * (for a particular namespace), a xcm_server() call will return an
 * error and set errno to EPROTO. The application may choose to retry
 * at a later time.
 *
 * @subsubsection tls_attr TLS Socket Attributes
 *
 * TLS has all the TCP-level attributes of the TCP transport; see
 * @ref tcp_attr.
 *
 * Attribute Name          | Socket Type | Value Type  | Mode | Description
 * ------------------------|-------------|-------------|------|------------
 * tls.peer_subject_key_id | Connection  | String      | R    | The X509v3 Subject Key Identifier of the remote peer, or a zero-length string in case the TLS connection is not established.
 *
 * @subsection utls_transport UTLS Transport
 *
 * The UTLS transport provides a hybrid transport, utilizing both the
 * TLS and UX transports internally for actual connection
 * establishment and message delivery.
 *
 * On the client side, at the time of xcm_connect(), the UTLS
 * transport determines if the server socket can be reached by using
 * the UX transport (i.e. if the server socket is located on the same
 * OS instance, in the same network namespace). If not, UTLS will
 * attempt to reach the server by means of the TLS transport.
 *
 * For a particular UTLS connection, either TLS or UX is used (never
 * both). XCM connections to a particular UTLS server socket may be a
 * mix of the two different types.
 *
 * For an UTLS server socket with the address <tt>utls:<ip>:<port></tt>,
 * two underlying addresses will be allocated;
 * <tt>tls:<ip>:<port></tt> and <tt>ux:<ip>:<port></tt>.
 *
 * Or, in the case DNS is used:
 * <tt>tls:<hostname>:<port></tt> and <tt>ux:<hostname>:<port></tt>.
 *
 * @subsubsection utls_limitations UTLS Limitations
 *
 * A wildcard should never be used when creating a UTLS server socket.
 *
 * If a DNS hostname is used in place of the IP address, both the
 * client and server need employ DNS, and also agree upon which
 * hostname to use (in case there are several pointing at the same IP
 * address).
 *
 * Failure to adhere to the above two rules will prevent a client from
 * finding a local server. Such a client will instead establish a TLS
 * connection to the server.
 *
 * @subsection sctp_transport SCTP Transport
 *
 * The SCTP transport uses the Stream Control Transmission Protocol
 * (SCTP). SCTP provides a reliable, message-oriented
 * service. In-order delivery is optional, but to adhere to XCM
 * semantics (and for other reasons) XCM leaves SCTP in-order delivery
 * enabled.
 *
 * The SCTP transport utilizes the native Linux kernel's
 * implementation of SCTP, via the BSD Socket API. The operating mode
 * is such that there is a 1:1-mapping between an association and a
 * socket (fd).
 *
 * The SCTP transport supports IPv4 and IPv6.
 *
 * To minimize latency, the SCTP transport disables the Nagle
 * algorithm.
 *
 * @section namespaces Linux Network and IPC Namespaces
 *
 * Namespaces is a Linux kernel facility concept for creating multiple,
 * independent namespaces for kernel resources of a certain kind.
 *
 * Linux Network Namespaces will affect all transports, including
 * the UX transport.
 *
 * XCM has no explicit namespace support. Rather, the application is
 * expected to use the Linux kernel facilities for this functionality
 * (i.e. switch to the right namespace before xcm_server() och
 * xcm_connect()).
 *
 * In case the system follows the iproute2 conventions in regards to
 * network namespace naming, the TLS and UTLS transports support
 * per-network namespace TLS certificates and private keys.
 *
 * @section limitations Limitations
 *
 * XCM, in its current form, does not support binding to a local
 * socket before doing connect() - something that is possible with BSD
 * Sockets, but very rarely makes sense.
 *
 * XCM also doesn't have a sendmmsg() or recvmmsg() equivalent. Those
 * could easily be added, and would provide some major performance
 * improvements for applications that are sending or receiving
 * multiple messages on the same connection on the same time. *mmsg()
 * equivalents have been left out because there are strong doubts
 * there are such applications.
 */

/*!
 * @file xcm.h
 * @brief This file contains the core Extensible Connection-oriented Messaging (XCM) API.
 */

#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>

#include <xcm_attr_map.h>

/** Flag used in xcm_connect() */
#define XCM_NONBLOCK (1<<0)

/** Struct representing an endpoint for communication.
 *
 * This endpoint can either be a server socket (created with
 * xcm_server(), or a connection socket, created as a result of a
 * xcm_accept() or xcm_connect() call.
 */
struct xcm_socket;

/** Connects to a remote server socket.
 *
 * This function returns a connection socket, which is used to send
 * messages to, and receive messages from the server.
 *
 * In BSD Sockets terms, this call does both socket() and connect().
 *
 * By default, xcm_connect() blocks for the time it takes for the
 * transport to determine if the named remote endpoint exists, and is
 * responding (including any initial handshaking, key exchange
 * etc). If the remote server socket is not yet bound, it's up to the
 * application to retry.
 *
 * If the XCM_NONBLOCK flag is set, xcm_connect() will work in a
 * non-blocking fashion and will always return immediately, either
 * leaving the connection socket in a connected state, a
 * partly connected state, or signaling an error.
 *
 * Setting XCM_NONBLOCK will leave the connection in non-blocking mode
 * (see xcm_set_blocking() for details).
 *
 * See @ref select for an overview how non-blocking mode is used.
 *
 * For non-blocking connection establishment attempts, the application
 * may use xcm_finish() the query the result. It should use xcm_fd()
 * and select() to wait for the appropriate time to make the
 * xcm_finish() call (although it may be called at any point).
 *
 * xcm_connect() with the XCM_NONBLOCK flag set will leave the
 * connection in non-blocking mode (see xcm_set_blocking() for
 * details).
 *
 * @param[in] remote_addr The remote address which to connect.
 * @param[in] flags Either 0, or XCM_NONBLOCK for a non-blocking connect.
 *
 * @return Returns a socket reference on success, or NULL if an error occured
 *         (in which case errno is set).
 *
 * errno        | Description
 * -------------|------------
 * EINVAL       | Invalid address format.
 * ENOPROTOOPT  | Transport protocol not available.
 * EMFILE       | The limit on the total number of open fds has been reached.
 * ENOENT       | DNS domain name resolution failed.
 *
 * See xcm_finish() for other possible errno values.
 *
 * @see xcm_close
 */

struct xcm_socket *xcm_connect(const char *remote_addr, int flags);

/** Connects to a remote server socket, with attributes.
 *
 * This function is equivalent to xcm_connect(), only it also allows
 * the caller to specify a set of @ref attributes to be applied as a
 * part of the connection establishment.
 *
 * The primary reasons for this function is to allow setting
 * attributes that needs to be set prior to, or during, actual
 * connection establishment. In addition, xcm_connect_a() serves as
 * a convenience function, letting applications avoid repeated
 * xcm_attr_set() calls.
 *
 * @param[in] remote_addr The remote address which to connect.
 * @param[in] attrs A set of attributes to be applied to the connection socket, or NULL.
 *
 * @return Returns a socket reference on success, or NULL if an error occured
 *         (in which case errno is set).
 * 
 * See xcm_connect() and xcm_attr_set() for possible errno values.
 */

struct xcm_socket *xcm_connect_a(const char *remote_addr,
				 const struct xcm_attr_map *attrs);

/** Creates a server socket and binds it to a specific address.
 *
 * This function creates a server socket and binds it to a specific
 * address. After this call has completed, clients may connect to the
 * address specified.
 *
 * This call is the equivalent of socket()+bind()+listen() in BSD
 * Sockets. In case remote_addr has a DNS domain name (as opposed to
 * an IP address), a xcm_server() call also includes a blocking name
 * resolution (e.g. gethostbyname()).
 *
 * @param[in] local_addr The local address to which this socket should be bound.
 *
 * @return Returns a server socket reference on success, or NULL if an
 *         error occured (in which case errno is set).
 *
 * errno        | Description
 * -------------|------------
 * EACCESS      | Permission to create the socket is denied.
 * EADDRINUSE   | Local socket address is already in use.
 * ENOMEM       | Insufficient memory.
 * EINVAL       | Invalid address format.
 * ENOPROTOOPT  | Transport protocol not available.
 * EMFILE       | The limit on the total number of open fds has been reached.
 * EPROTO       | A protocol error occured.
 * ENOENT       | DNS domain name resolution failed.
 *
 * @see xcm_close
 */

struct xcm_socket *xcm_server(const char *local_addr);

/** Creates and binds to a server socket, with attributes.
 *
 * This function is equivalent to xcm_server(), only it also allows
 * the caller to specify a set of @ref attributes to be applied as a
 * part of server socket creation.
 *
 * @param[in] local_addr The local address to which this socket should be bound.
 * @param[in] attrs A set of attributes to be applied to the socket, or NULL.
 *
 * @return Returns a server socket reference on success, or NULL if an
 *         error occured (in which case errno is set).
 *
 * See xcm_server() and xcm_attr_set() for possible errno values.
 */

struct xcm_socket *xcm_server_a(const char *local_addr,
				const struct xcm_attr_map *attrs);

/** Close an endpoint.
 *
 * This function close a XCM socket, including both signaling to the far
 * and freeing of any local resources associated with this socket.
 *
 * xcm_close() will not block, and applications wanting to finish any
 * outstanding tasks on a socket in non-blocking mode should use
 * xcm_finish() to do so.
 *
 * @param[in] socket The socket to be closed, or NULL (in case xcm_close() is a no-operation).
 *
 * @return Returns 0 on success, or -1 if an error occured
 *         (in which case errno is set).
 *
 * @see xcm_cleanup
 */
int xcm_close(struct xcm_socket *socket);

/** Cleans up any local resources tied to a XCM socket not owned by the caller process.
 *
 * After a fork() call, either of the two processes (the parent, or the
 * child) must be designated the owner of every XCM socket the parent
 * owned.
 *
 * The owner may continue to use the XCM socket normally.
 *
 * The non-owner may use xcm_cleanup() to free any local memory tied to
 * this socket, without impacting the connection state in the owner
 * process.
 *
 * The non-owner may not call xcm_close() or any other XCM API call.
 *
 * The owner may not call xcm_cleanup().
 *
 * @param[in] socket The socket which local resources are to be freed, or NULL (in case xcm_cleanup() is a no-operation).
 */

void xcm_cleanup(struct xcm_socket *socket);

/** Retrieve a pending incoming connection from the server socket's queue.
 *
 * xcm_accept() retrieves the first connection request from the server
 * socket's queue of pending connections.
 *
 * In case the server socket is in non-blocking mode, the XCM
 * connection socket returned from xcm_accept() will also be in non-blocking
 * mode.
 *
 * @param[in] server_socket The server socket on which to attempt to accept
 *                          one pending connection.
 *
 * @return Returns a newly created XCM connection socket on success,
 *         or NULL if an error occured (in which case errno is set).
 *
 * errno        | Description
 * -------------|------------
 * EMFILE       | The limit on the total number of open fds has been reached.
 *
 * See xcm_finish() for other possible errno values.
 */

struct xcm_socket *xcm_accept(struct xcm_socket *server_socket);

/** Retrieve a pending incoming connection, with attributes.
 *
 * This function is equivalent to xcm_accept(), only it also allows
 * the caller to specify a set of @ref attributes to be applied as a
 * part of accepting the new connection socket.
 *
 * @param[in] server_socket The server socket on which to attempt to accept
 *                          one pending connection.
 * @param[in] attrs A set of attributes to be applied to the socket, or NULL.
 *
 * @return Returns a newly created XCM connection socket on success,
 *         or NULL if an error occured (in which case errno is set).
 *
 * See xcm_accept() and xcm_attr_set() for possible errno values.
 */

struct xcm_socket *xcm_accept_a(struct xcm_socket *server_socket,
				const struct xcm_attr_map *attrs);

/** Send message on a particular connection.
 *
 * The xcm_send() function is used to send a message out on a
 * connection socket. A XCM connection goes from a client to a server,
 * and this connection socket may represent either one of the two
 * endpoints.
 *
 * @param[in] conn_socket The connection socket the message will be sent on.
 * @param[in] buf A pointer to the message data buffer.
 * @param[in] len The length of the message in bytes. Zero-length messages are not allowed.
 *
 * @return Returns 0 on success, or -1 if an error occured
 *         (in which case errno is set).
 *
 * errno        | Description
 * -------------|------------
 * EMSGSIZE     | Message is too large. See also @ref xcm_attr.
 *
 * See xcm_finish() for more errno values.
 */

int xcm_send(struct xcm_socket *conn_socket, const void *buf, size_t len);

/** Receive message on a particular connection.
 *
 * The xcm_receive() function is used to receive message on a
 * connection socket. A XCM connection goes from a client to a server,
 * and this connection socket may represent either one of the two
 * endpoints.
 *
 * If the capacity of the user-supplied buffer is smaller than the
 * actual message length, the message will be truncated and the part
 * that fits will be stored in the buffer. The return value will be
 * the length of the truncated message (i.e. the capacity).
 *
 * @param[in] conn_socket The connection socket the message will receive be on.
 * @param[out] buf The user-supplied buffer where the incoming message will be stored.
 * @param[in] capacity The capacity in bytes of the buffer.
 *
 * @return Returns the size (> 0 bytes) of the received message, 0 if
 *         the remote end has closed the connection, or -1 if an error
 *         occured (in which case errno is set).
 *
 * See xcm_finish() for possible errno values.
 */
int xcm_receive(struct xcm_socket *conn_socket, void *buf, size_t capacity);

/** Flag bit denoting a socket where the application likely can
    receive a message. */
#define XCM_SO_RECEIVABLE (1<<0)
/** Flag bit denoting a socket where the application likely can
    send a message. */
#define XCM_SO_SENDABLE (1<<1)
/** Flag bit denoting a socket with a pending incoming connection. */
#define XCM_SO_ACCEPTABLE (1<<2)

/** Inform socket of which operations the application is waiting to
 *  perform.
 *
 * This function is only used by event-driven application and with XCM
 * sockets in non-blocking mode. For an overview on this subject, see
 * @ref select.
 *
 * Using xcm_await(), the application informs the XCM socket what
 * conditions it's waiting for (i.e. what XCM operations it wants to
 * perform). These conditions are stored in the socket, and won't
 * change until the application calls xcm_await() again.
 *
 * The @p condition parameter is a bitmask, with the valid bits being
 * @ref XCM_SO_RECEIVABLE or @ref XCM_SO_SENDABLE (for connection
 * socket) or @ref XCM_SO_ACCEPTABLE (for server sockets). If no bits
 * are set, the application is not interested in anything beyond the
 * XCM socket to finish any outstanding tasks.
 *
 * Typically, the application would call xcm_await() when an XCM
 * operation (such as xcm_receive()) has failed with errno set to
 * EAGAIN. However, the application may also call xcm_await() even
 * though neither xcm_send(), xcm_receive(), nor xcm_finish() has
 * failed in such a manner.
 *
 * In case any of the conditions the application is asking for are
 * believed to be met already at the time of the xcm_await() call, the
 * XCM socket fd (see xcm_fd() for details) will be marked as ready to
 * be read.
 *
 * The conditions specified by the application are future operation it
 * wishes to perform on a socket (as opposed to finishing operations
 * the socket has already accepted). For example, if an application
 * use xcm_send() to transmit a message, and the XCM socket accept
 * this request (by returning 0 on the call), the application
 * shouldn't send @ref XCM_SO_SENDABLE flag for the reason of having
 * XCM finishing the transmission; the task of actually handing over
 * message to the lower layer is performed by XCM regardless of the
 * conditions specified.
 *
 * Even though XCM socket fd is marked readable (by select()), and
 * thus the application-specified conditions for a particular
 * connection socket are likely met, there's no guarantee that the API
 * operation (i.e. xcm_send(), xcm_receive() or xcm_accept()) will
 * succeed.
 *
 * If an application is waiting for both XCM_SO_SENDABLE and
 * XCM_SO_RECEIVABLE, is should try both to send and receive when the
 * socket fd is marked readable.
 *
 * @param[in] socket The XCM socket.
 * @param[in] condition The condition the application is waiting for.
 *
 * @return Returns the XCM socket fd on success, or -1 if an error
 *         occured (in which case errno is set).
 *
 * errno        | Description
 * -------------|------------
 * EINVAL       | The socket is not in non-blocking mode, or the condition bits are invalid.
 */

int xcm_await(struct xcm_socket *socket, int condition);

/** Returns XCM socket fd.
 *
 * This call retrieves the XCM socket fd for a XCM socket non-blocking
 * mode.
 *
 * When this fd becomes readable, the XCM socket is ready to make
 * progress.
 *
 * Progress can mean both progress toward the goal of reaching the
 * application's desired socket condition (see xcm_await() for
 * details), or finishing any outstanding task the XCM socket has.
 *
 * Please note that the XCM socket fd is @b only ever marked readable
 * (as opposed to writable). This is true even if the application is
 * waiting to send a message on the socket. Marked readable means that
 * the fd is, for example, marked with EPOLLIN, in case epoll_wait()
 * is used, or has its bit set in the @p readfs fd_set, in case
 * select() is used.
 *
 * When the XCM socket fd becomes readable, an application would
 * typically perform the actions it specified in xcm_await()'s @ref
 * condition parameter. It is not forced to do so, but may choose to
 * perform other API operations instead. However, if neither
 * xcm_send() nor xcm_receive() is called, the application must call
 * xcm_finish(). The xcm_finish() call must be made, even though the
 * @p condition parameter was set to zero. This is to allow the socket
 * make progress on its background tasks. See @ref outstanding_tasks
 * for details.
 *
 * @param[in] socket The connection or server socket.
 *
 * @return Returns the XCM socket fd on success, or -1 if an error
 *         occured (in which case errno is set).
 *
 * errno        | Description
 * -------------|------------
 * EINVAL       | The socket is not in non-blocking mode.
 *
 * @see xcm_await
 */

int xcm_fd(struct xcm_socket *socket);

/** Attempts to finish an ongoing non-blocking background operation.
 *
 * This call is used by an application having issued xcm_connect()
 * with the XCM_NONBLOCK flag set, xcm_accept() or xcm_send() call on
 * a connection socket in non-blocking mode, wishing to finish
 * outstanding processing related to that operation, to know if it
 * succeeded or not.
 *
 * In addition, xcm_finish() must be called if the conditions on a
 * non-blocking socket are met (as signaled by select() marking the
 * socket fd returned by xcm_fd() as readable), unless the application
 * calls xcm_send(), xcm_receive() or xcm_accept() on that socket. See
 * @ref outstanding_tasks for details.
 *
 * xcm_finish() may be called at any time.
 *
 * @param[in] socket The connection or server socket.
 *
 * @return Returns 0 if the connection has been successfully been
 *         established, or -1 if it has not (in which case errno is
 *         set).
 *
 * These errno values are possible not only for xcm_finish(), but also
 * for xcm_connect(), xcm_accept(), xcm_send(), and xcm_receive().
 *
 * errno        | Description
 * -------------|------------
 * EPIPE        | The connection is closed.
 * EAGAIN       | The socket is marked non-blocking (with xcm_set_blocking()) and the requested operation would block.
 * ECONNRESET   | Connection reset by peer.
 * ECONNREFUSED | No-one is listening on the remote address.
 * ECONNABORTED | A connection has been aborted due to host-internal reasons.
 * EHOSTUNREACH | Remote host is unreachable.
 * ENETUNREACH  | Network is unreachable.
 * ETIMEDOUT    | No or lost network connectivity.
 * ENOMEM       | Insufficient memory (or other resources) to perform operation.
 * EINTR        | The operation was interrupted by a UNIX signal.
 * EPROTO       | A non-recoverable protocol error occurred.
 */

int xcm_finish(struct xcm_socket *socket);

/** Enabled or disabled non-blocking operation on this socket.
 *
 * In blocking mode (which is the default), xcm_send() and
 * xcm_receive() calls does not return until a message has been handed
 * over to the system (in case of send), or received from the system
 * (in case of receive), or an error has occured (whichever happens
 * first).
 *
 * In non-blocking mode, xcm_send() and xcm_receive() will return
 * immediately, regardless if XCM has been enable to fulfill the
 * application's request or not.
 *
 * Server sockets may also be set into non-blocking mode, in which
 * case xcm_accept() won't block.

 * Connection sockets created as a result of xcm_connect() may be set
 * into non-blocking mode already from the start, by means of the @ref
 * XCM_NONBLOCK flag to xcm_connect(), in which case also the
 * connection establishment process is non-blocking.
 *
 * For an overview of the use of non-blocking mode, see @ref select.
 *
 * To set a non-blocking connection socket into blocking mode, it
 * needs to have finished all outstanding tasks. See @ref
 * outstanding_tasks for details.
 *
 * Setting the "xcm.blocking" attribute is an alternative to using
 * this function. See @ref xcm_attr.
 *
 * @param[in] socket The socket.
 * @param[in] should_block Set to true for blocking operation, false
 *                         for non-blocking mode.
 *
 * @return Returns the 0 on success, or -1 if an error occured
 *         (in which case errno is set).
 *
 * errno        | Description
 * -------------|------------
 * EAGAIN       | The connection socket has unfinished work that needs to completed before mode can be switched.
 */
int xcm_set_blocking(struct xcm_socket *socket, bool should_block);

/** Query whether or not a socket is in non-blocking mode.
 *
 * For an overview of the use of non-blocking mode, see @ref select.
 *
 * Reading the "xcm.blocking" attribute is an alternative to
 * using this function. See @ref xcm_attr.
 *
 * @param[in] socket The socket.
 *
 * @return Returns the true if the socket is in blocking mode, or false
 *         if it is in non-blocking mode.
 *
 * @see xcm_set_blocking
 */
bool xcm_is_blocking(struct xcm_socket *socket);

/** Returns the address of the remote endpoint for this connection.
 *
 * This operation only works for sockets representing connections.
 *
 * The address returned is in string format, and the pointer returned
 * is to an buffer allocated as a part of the socket state, and need
 * not and should not be free'd by the user.
 *
 * Reading the "xcm.remote_addr" attribute is an alternative to using
 * this function. See @ref xcm_attr.
 *
 * @param[in] conn_socket The connection socket.
 *
 * @return Returns the remote endpoint address, or NULL if an error
 *         occurred (in which case errno is set).
 */
const char *xcm_remote_addr(struct xcm_socket *conn_socket);

/** Returns the address of the local endpoint for this socket.
 *
 * Just like xcm_remote_addr(), but returns the local endpoint address.
 *
 * This function applies to both server and connection sockets.
 *
 * Reading the "xcm.local_addr" attribute is an alternative to using
 * this function. See @ref xcm_attr.
 *
 * @param[in] socket A server or connection socket.
 *
 * @return Returns the local endpoint address, or NULL if an error
 *         occurred (in which case errno is set).
 */
const char *xcm_local_addr(struct xcm_socket *socket);

#include <xcm_compat.h>

#ifdef __cplusplus
}
#endif
#endif
