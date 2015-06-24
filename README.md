Apache Traffic Server redis-based remap plugin
==============================================

This is a plugin that allows us to do dynamic reverse proxy (dynamic remap) based on the
information present in the redis key/value store.

It is based on the mysql_remap plugin code:
(https://github.com/apache/trafficserver/tree/master/plugins/experimental/mysql_remap)
and the memcached_remap plugin which was also based on the mysql_remap code:
https://github.com/apache/trafficserver/tree/master/plugins/experimental/memcached_remap


                                          (GET key )
[Browser] <----> [Apache Traffic Server] <----------> [Backend Servers]
                            (redis returns the backend as the value))

redis is only used as the key/value store.  An external process must populate
the keys and values appropriately.

Prerequisites
==============================================

1. Download, compile and install redis 3.x
* http://download.redis.io/releases/redis-<version>.tar.gz
2.  Download, compile and install hiredis C client library for redis
* git://github.com/redis/hiredis.git

3. Compile and Install the redis_remap plugin

make
sudo make install

4. Start the Traffic Server

/usr/local/bin/traffic_server

5. Start the redis

sudo service redis start

6. Start apache on port 8080 (change configs accordingly)

sudo service httpd start

7. Add some keys into the redis store for testing (via python)

TODO: Update these to test domains instead of my own

```python
import redis

hosts = { 'https://sfeltner.com:443/' : 'http://104.238.126.142',
          'https://www.sfeltner.com:443/' : 'http://104.238.126.142',
          'https://forum.sfeltner.com:443/' : 'http://104.238.126.142',
          'https://kevinfeltner.com:443/' : 'http://104.238.126.142',
          'https://thefeltners.com:443/' : 'http://104.238.126.142',
          'https://[a-z0-9-]*[.]?n2deep.co:443/' : 'http://104.238.77.182',
          'https://divedeepstaylong.com:443/' : 'http://198.12.155.76',
          'https://www.divedeepstaylong.com:443/' : 'http://198.12.155.76',
        }

# connect to local server
r = redis.StrictRedis(host='localhost', port=6379, db=0)

for src, dst in hosts.iteritems():
    r.set(src, dst)

for each in hosts:
    print("Fetching: [{0}] [{1}]".format(each, r.get(each)))
```

```
$ python sample.py
Fetching: [https://kevinfeltner.com:443/] [http://104.238.126.142]
Fetching: [https://thefeltners.com:443/] [http://104.238.126.142]
Fetching: [https://www.sfeltner.com:443/] [http://104.238.126.142]
Fetching: [https://forum.sfeltner.com:443/] [http://104.238.126.142]
Fetching: [https://[a-z0-9-]*[.]?n2deep.co:443/] [http://104.238.77.182]
Fetching: [https://sfeltner.com:443/] [http://104.238.126.142]
Fetching: [https://www.divedeepstaylong.com:443/] [http://198.12.155.76]
Fetching: [https://divedeepstaylong.com:443/] [http://198.12.155.76]
```



8. Do sample query

```
$ curl -isSL https://n2deep.co
HTTP/1.1 200 OK
Date: Wed, 24 Jun 2015 00:20:59 GMT
Server: ATS
Last-Modified: Tue, 12 Aug 2014 20:43:20 GMT
ETag: "21c57-31c-50074b8c9ea00"
Accept-Ranges: bytes
Content-Length: 796
Content-Type: text/html; charset=UTF-8
Age: 0
Connection: keep-alive
Via: http/1.1 s104-238-126-142.secureserver.net (ApacheTrafficServer/5.3.1 [csSf ])
```


7. Thats it !

#############################
#  SUPPORTED KEYS IN REDIS  #
#############################

The advantage of redis is that, its a hash table.

KEY FORMAT:    [PROTOCOL]://[SERVER]:[PORT]/
--------------------------------------------
(Even ending / is important!)

PROTOCOL can be any word in a-z, A-Z chars
SERVER   can be any string which isn't a colon character, including a regex
         that could be used in a regex_map 
PORT     should be a number
/        should end with a slash

VALUE FORMAT:  [PROTOCOL]://[SERVER]:[PORT]/
--------------------------------------------
(Even ending / is important!)

PROTOCOL can be any word created from a-z, A-Z chars
SERVER   can be any string which isn't a colon character
PORT     should be a number
/        should end with a slash
