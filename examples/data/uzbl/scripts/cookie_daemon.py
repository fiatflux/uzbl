#!/usr/bin/env python

# Uzbl tabbing wrapper using a fifo socket interface
# Copyright (c) 2009, Tom Adams <tom@holizz.com>
# Copyright (c) 2009, Dieter Plaetinck <dieter AT plaetinck.be>
# Copyright (c) 2009, Mason Larobina <mason.larobina@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


import cookielib
import os
import sys
import urllib2
import select
import socket
import time

try:
    import cStringIO as StringIO

except ImportError:
    import StringIO


# ============================================================================
# ::: Default configuration section ::::::::::::::::::::::::::::::::::::::::::
# ============================================================================

# Location of the uzbl cache directory.
if 'XDG_CACHE_HOME' in os.environ.keys() and os.environ['XDG_CACHE_HOME']:
    cache_dir = os.path.join(os.environ['XDG_CACHE_HOME'], 'uzbl/')

else:
    cache_dir = os.path.join(os.environ['HOME'], '.cache/uzbl/')

# Location of the uzbl data directory.
if 'XDG_DATA_HOME' in os.environ.keys() and os.environ['XDG_DATA_HOME']:
    data_dir = os.path.join(os.environ['XDG_DATA_HOME'], 'uzbl/')

else:
    data_dir = os.path.join(os.environ['HOME'], '.local/share/uzbl/')

# Create cache dir and data dir if they are missing.
for path in [data_dir, cache_dir]:
    if not os.path.exists(path):
        os.makedirs(path) 

# Default config
cookie_socket = os.path.join(cache_dir, 'cookie_daemon_socket')
cookie_jar = os.path.join(data_dir, 'cookies.txt')
#daemon_timeout = 360
daemon_timeout = 0

# ============================================================================
# ::: End of configuration section :::::::::::::::::::::::::::::::::::::::::::
# ============================================================================


class CookieMonster:
    '''The uzbl cookie daemon class.'''

    def __init__(self, cookie_socket, cookie_jar, daemon_timeout):

        self.cookie_socket = os.path.expandvars(cookie_socket)
        self.server_socket = None
        self.cookie_jar = os.path.expandvars(cookie_jar)
        self.jar = None
        self.daemon_timeout = daemon_timeout
        self.last_request = time.time()

    
    def run(self):
        '''Start the daemon.'''
        
        try:
            # Daemonize process. 
            #self.daemonize()
        
            # Create cookie_socket 
            self.create_socket()
        
            # Create jar object
            self.open_cookie_jar()

            # Listen for GET and PULL cookie requests.
            self.listen()

        except:
            #raise
            print "%r" % sys.exc_info()[1]
        
        self.quit()


    def daemonize(function):
        '''Daemonize the process using the Stevens' double-fork magic.'''

        try:
            if os.fork(): sys.exit(0)

        except OSError, e:
            sys.stderr.write("fork #1 failed: %s\n" % e)
            sys.exit(1)
        
        os.chdir('/')
        os.setsid()
        os.umask(0)
        
        try:
            if os.fork(): sys.exit(0)

        except OSError, e:
            sys.stderr.write("fork #2 failed: %s\n" % e)
            sys.exit(1)
        
        sys.stdout.flush()
        sys.stderr.flush()

        devnull = '/dev/null'
        stdin = file(devnull, 'r')
        stdout = file(devnull, 'a+')
        stderr = file(devnull, 'a+', 0)

        os.dup2(stdin.fileno(), sys.stdin.fileno())
        os.dup2(stdout.fileno(), sys.stdout.fileno())
        os.dup2(stderr.fileno(), sys.stderr.fileno())
        

    def open_cookie_jar(self):
        '''Open the cookie jar.'''
        
        # Open cookie jar.
        self.jar = cookielib.MozillaCookieJar(cookie_jar)
        try:
            self.jar.load(ignore_discard=True)

        except:
            pass


    def create_socket(self):
        '''Open socket AF_UNIX socket for uzbl instance <-> daemon
        communication.'''
    
        if os.path.exists(self.cookie_socket):
            # Don't you just love racetrack conditions! 
            sys.exit(1)
            
        self.server_socket = socket.socket(socket.AF_UNIX,\
          socket.SOCK_SEQPACKET)

        self.server_socket.bind(self.cookie_socket)


    def listen(self):
        '''Listen for incoming cookie PUT and GET requests.'''

        while True:
            # If you get broken pipe errors increase this listen number.
            self.server_socket.listen(1)

            if bool(select.select([self.server_socket],[],[],1)[0]):
                client_socket, _ = self.server_socket.accept()
                self.handle_request(client_socket)
                self.last_request = time.time()
            
            if self.daemon_timeout:
                idle = time.time() - self.last_request
                if idle > self.daemon_timeout: break
        

    def handle_request(self, client_socket):
        '''Connection has been made by uzbl instance now to serve 
        cookie PUT or GET request.'''
         
        # Receive full request from client.
        data = client_socket.recv(4096)

        argv = data.split("\0")
        
        # For debugging:
        #print argv

        action = argv[0]
        set_cookie = argv[4] if len(argv) > 3 else None
        uri = urllib2.urlparse.ParseResult(
          scheme=argv[1],
          netloc=argv[2],
          path=argv[3],
          params='',
          query='',
          fragment='').geturl()
        
        req = urllib2.Request(uri)

        if action == "GET":
            self.jar.add_cookie_header(req)
            if req.has_header('Cookie'):
                client_socket.send(req.get_header('Cookie'))

            else:
                client_socket.send("\0")

        elif action == "PUT":
            hdr = urllib2.httplib.HTTPMessage(\
              StringIO.StringIO('Set-Cookie: %s' % set_cookie))
            res = urllib2.addinfourl(StringIO.StringIO(), hdr,\
              req.get_full_url())
            self.jar.extract_cookies(res,req)
            self.jar.save(ignore_discard=True)
            
        client_socket.close()


    def quit(self):
        '''Called on exit to make sure all loose ends are tied up.'''
        
        # Only one loose end so far.
        self.del_socket()
        
        # And die gracefully.
        sys.exit(0)
    

    def del_socket(self):
        '''Remove the cookie_socket file on exit. In a way the cookie_socket 
        is the daemons pid file equivalent.'''
    
        if self.server_socket:
            self.server_socket.close()

        if os.path.exists(self.cookie_socket):
            os.remove(self.cookie_socket)


if __name__ == "__main__":
    
    if os.path.exists(cookie_socket):
        print "Error: cookie socket already exists: %r" % cookie_socket
        sys.exit(1)
    
    CookieMonster(cookie_socket, cookie_jar, daemon_timeout).run()
