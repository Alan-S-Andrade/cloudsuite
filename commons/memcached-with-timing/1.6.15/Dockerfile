FROM cloudsuite/base-os:ubuntu

# This is based on the official memcached Dockerfile with added timing instrumentation for assoc_find()

# add our user and group first to make sure their IDs get assigned consistently, regardless of whatever dependencies get added
RUN groupadd -r memcache && useradd -r -g memcache memcache

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends libevent-2.1-7 && rm -rf /var/lib/apt/lists/*;

# Copy pre-modified memcached source with timing instrumentation for assoc_find()
COPY src /usr/src/memcached

RUN buildDeps='autoconf automake gcc libc6-dev libevent-dev m4 make perl' \
	&& set -x \
	&& apt-get update && apt-get install -y $buildDeps --no-install-recommends \
	&& rm -rf /var/lib/apt/lists/* \
	&& cd /usr/src/memcached \
	&& ./configure \
	&& make -j $(nproc) \
	&& make install \
	&& cd / && rm -rf /usr/src/memcached \
	&& apt-get purge -y --auto-remove $buildDeps

ENTRYPOINT ["memcached"]

USER memcache
EXPOSE 11211
