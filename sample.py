#!/usr/bin/python
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Author: opensource@navyaprabha.com
# Description: Sample script to add keys to memcached for use with YTS/memcached_remap plugin

import redis

hosts = { 'https://sfeltner.com:443/' : 'http://104.238.126.142',
          'https://www.sfeltner.com:443/' : 'http://104.238.126.142',
          'https://forum.sfeltner.com:443/' : 'http://104.238.126.142',
          'https://kevinfeltner.com:443/' : 'http://104.238.126.142',
          'https://thefeltners.com:443/' : 'http://104.238.126.142',
          'https://[a-z0-9-]*[.]?n2deep.co:443/' : 'http://104.238.77.182',
          'https://divedeepstaylong.com:443/' : 'http://198.12.155.76',
          'https://www.divedeepstaylong.com:443/' : 'http://198.12.155.76',
          'http://sfeltner.com:80/' : 'http://104.238.126.142:8080',
          'http://www.sfeltner.com:80/' : 'http://104.238.126.142:8080',
          'http://forum.sfeltner.com:80/' : 'http://104.238.126.142:8080',
          'http://kevinfeltner.com:80/' : 'http://104.238.126.142:8080',
          'http://thefeltners.com:80/' : 'http://104.238.126.142:8080',
          'http://[a-z0-9-]*[.]?n2deep.co:80/' : 'http://104.238.77.182:8080',
          'http://divedeepstaylong.com:80/' : 'http://198.12.155.76:8080',
          'http://www.divedeepstaylong.com:80/' : 'http://198.12.155.76',}

# connect to local server
r = redis.StrictRedis(host='localhost', port=6379, db=0)

for src, dst in hosts.iteritems():
    r.set(src, dst)


for each in hosts:
    print("Fetching: [{0}] [{1}]".format(each, r.get(each)))

