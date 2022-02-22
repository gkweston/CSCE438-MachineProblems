# csce438/mp2:dev_env
# --------------------------- NOTES --------------------------------
# Provision the build env with ./build_env/Dockerfile then, from
# this directory, run:
# 
# `docker build -t csce438/mp2:dev_env .`
# 
# (EITHER) Enter the container's shell with:
# `docker run -it csce438/mp2:test /bin/bash`
# 
# MISC:
# For color based tty see:
# https://stackoverflow.com/questions/33493456/docker-bash-prompt-does-not-display-color-output
# 
# For copying files w/o new build see:
# https://stackoverflow.com/questions/22907231/how-to-copy-files-from-host-to-docker-container
# ------------------------------------------------------------------
FROM csce438/mp2:build_env

RUN mkdir /root/build
COPY ./build /root/build/

# RUN cd /root/build; \
    # make;
