FROM cloudsuite/memcached-with-timing:1.6.15

# the entry point is set to memcached in the base image
# Memcached runs with timing instrumentation for assoc_find() function
CMD ["-t", "2", "-m", "2048", "-n", "550", "-vv"]
