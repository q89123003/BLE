#!/usr/bin/env python
"""
# BLE Data Center Module

This module use socket (AF_INET or AF_UNIX) to communicate with BLE agent

If directly invoked via command line, this program serves as storage
daemon; otherwise, this module can be seen as an abstract layer for
storage.

# Dependencies

* bcrypt
* pymongo (only if in mongo mode)

# Command-line Usage

1. This program by default starts in file mode. To start in MongoDB mode,
   set '-m' or '--mongo' flag along with '--dbloc' and '--dbcol' arguments

   Alternatively, set '--file' argument to load credentials.

2. You should provide either '--port' with port number to listen on TCP
   socket, or '--socket' with Unix socket file to listen on Unix socket.

## Examples

* python auth.py --mongo --dbloc localhost --dbname ble --port 8888
  (connect to 'localhost:27017' with database name 'ble', AF_INET socket on
   localhost:8888)

# Module Usage

See function 'server_generator' and 'handler_generator' for initialization

"""

import argparse
import socketserver


__all__ = ['mac_str_to_bytes', 'mac_bytes_to_str',
           'handler_generator', 'server_generator']


def mac_str_to_bytes(mac):
    """
    Convert MAC string representation to bytes
    '5c:e0:c5:cc:46:79' => b'\\\xe0\xc5\xccFy'
    """
    return bytes(map(lambda x: int(x, 16), mac.split(':')))


def mac_bytes_to_str(mac):
    """
    COnvert MAC bytes to MAC string representation
    b'\\\xe0\xc5\xccFy' => '5c:e0:c5:cc:46:79'
    """
    return ':'.join('%02x' % x for x in mac)


def handler_generator(backend_type, **kwargs):
    """
    This function create a new handler class to be used by socket server.
    In static mode, all user/pswd pairs are given by caller of this function,
    So the credential source are not limited to files.
    Parameter:
        backend_type    Possible values are either 'static' or 'mongo'

    Extra parameter for backend_type == 'static'
        repo            The dict in which name/password pairs stored

    Extra parameter for backend_type == 'mongo'
        conn            The address to the mongodb service
    """
    if backend_type == 'static':
        print('Backend: static')
        if not kwargs['fpath']:
            raise ValueError(
                'Lacks extra parameter \'fpath\' for backend_type=static')

        fpath = kwargs['fpath']

        def handle(self):
            """Static backend handler"""
            msg = self.request.recv(22)
            print(msg)
            mac, sensor_type, data = msg[:17], msg[17:18], msg[18:]
            with open(fpath, 'ab') as f:
                f.write(mac)
                f.write(b',')
                f.write(sensor_type)
                f.write(b',')
                f.write(data)
                f.write(b'\n')

            self.request.sendall(b'Done')

        return type('StaticHandler', (socketserver.BaseRequestHandler,), {'handle': handle})

    elif backend_type == 'mongo':
        print('Backend: MongoDB')
        if not kwargs['conn']:
            raise ValueError(
                'Lacks extra parameter \'conn\' for backend_type=mongo')

        conn = kwargs['conn']

        def handle(self):
            """mongo backend handler"""
            msg = self.request.recv(22)
            print(msg)
            mac, sensor_type, data = msg[:17], msg[17:18], msg[18:]
            doc = conn.insert_one(
                {'mac': mac, 'sensor_type': sensor_type, 'data': data})
            if doc.inserted_id:
                res = b'Success'
            else:
                res = b'Failed'
            self.request.sendall(res)

        return type('MongoHandler', (socketserver.BaseRequestHandler,), {'handle': handle})
    else:
        raise ValueError(
            'Invalid backend_type {0} provided'.format(backend_type))


def server_generator(socket_type, addr, handler):
    """
    This function start a server daemon that will listen on specific address
    Parameter:
        socket_type     Possible values are either 'AF_UNIX' or 'AF_INET'
        address         Depends on socket_type, it can be a path in the file
                            system (AF_UNIX case) or a (HOST, PORT) tuple
                            (AF_INET case)
        handler         A class derived from socketserver.BaseRequestHandler
                            that would be initialize and execute on each
                            request
    """
    if socket_type == 'AF_UNIX':
        print('Using AF_UNIX socket')
        return socketserver.ThreadingUnixStreamServer(addr, handler)
    elif socket_type == 'AF_INET':
        print('Using AF_INET socket')
        return socketserver.ThreadingTCPServer(addr, handler)
    else:
        raise ValueError(
            'Invalid socket_type {0} provided'.format(socket_type))


def get_server(args, handler):
    """Return server by arguments"""
    if args['socket']:
        return server_generator('AF_UNIX', args['socket'], handler)
    elif args['port']:
        return server_generator('AF_INET', ('', args['port']), handler)
    else:
        raise ValueError('Either socket or port should be specified.')


def main_mongo(args):
    """
    Mongo entry point
    Create a connection to the database. Authentication by query.
    Unlike file mode, adding new user is possible.
    """
    print('Start in mongo mode with DB location {0}, name {1}'
          .format(args['dbloc'], args['dbname']))

    # Import in the middle of file because pymongo shouldn't become a
    # depencency if this module is used in file mode.
    from pymongo import MongoClient
    client = MongoClient('mongodb://{0}'.format(args['dbloc']))

    if args['dbname'] not in client.database_names():
        raise ValueError('DB name {0} not exists in location {1}'.format(
            args['dbname'], args['dbloc']))

    db = client[args['dbname']]

    if 'users' not in db.collection_names():
        raise ValueError(
            'DB \'{0}\' in location {1} doesn\'t have collection \'users\''.format(
                args['dbname'], args['dbloc']))

    handler = handler_generator('mongo', conn=db['users'])
    server = get_server(args, handler)

    server.serve_forever()


def main_file(args):
    """
    File entry point
    Load a credential file into memory. Authenticate based on that.
    There is no way to add new user/password pair except restarting this daemon.
    """
    print('Start in file mode with path {0}'.format(args['file']))

    handler = handler_generator('static', fpath=args['file'])
    server = get_server(args, handler)

    server.serve_forever()


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='An authencation daemon for BLE agent.')
    parser.add_argument('-m', '--mongo', action='store_true',
                        help='start in mongodb mode (default is in file mode)')

    parser.add_argument('--file', nargs='?', default=None,
                        help='the path of the credential file (in file mode)')
    parser.add_argument('--dbloc', nargs='?', default=None,
                        help='the db location (in mongodb mode)')
    parser.add_argument('--dbname', nargs='?', default=None,
                        help='the db name which contains \'user\' collection(in mongodb mode)')

    parser.add_argument('-p', '--port', nargs='?', default=None, type=int,
                        help='server listening port')
    parser.add_argument('-s', '--socket', nargs='?', default=None,
                        help='unix_socket path (also ignore --port)')

    args = vars(parser.parse_args())

    if args['mongo']:
        if not args['dbloc'] or not args['dbname']:
            raise ValueError(
                'Entering mongo mode but DB location or DB name are not set')
        main_mongo(args)
    else:
        if not args['file']:
            raise ValueError('Entering file mode but file path is not set')
        main_file(args)


if __name__ == '__main__':
    main()
