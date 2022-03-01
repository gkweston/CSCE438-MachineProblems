# CSCE438 MP2 — <font size="3">tiny</font> Social Networking Service

## Compilation & Use
This project is intended for deployment on an amazonlinux environment with Google's gRPC and Protocol Buffers installed. We're using Docker to build/deploy this environment which allows arbitrary resource allocation and platform independance in development, but use with Amazon's Cloud9 is also supported.

### Building Docker image

After ensuring Docker is properly installed, download these files via `git clone` or some other means into your `$WORKING_DIR`. Then run:
    
    cd ${THIS_REPO}/MP_2/build_env
    docker build -t csce438/mp2:dev_env .

This may take a while, but it will pull an amazonlinux image and install all project dependancies. Keep the `csce438/mp2:dev_env` image on hand as we will use Docker caching to quickly spin up dev containers. 

### Starting Docker container for development/testing
Next spin up the dev container by running

    cd ..
    sh start_dev_env.sh

This will build the dev container (with all project files) and save the container's hash to `.dev_container` so we can automate the save/shutdown procedure.

In the VSCode window that opens automatically navigate to the "Remote Explorer" tab and remote into `csce438/mp2:dev`. 

Finally, install any VSCode extensions. You should save these in `MP_2/.vscode-server` on the host machine so the startup script can copy them and you don't have to reinstall extensions every time.

Open as many shells on the dev environment as you need by opening a new bash instance, navigating to the project directory and running:

    docker exec -it $(cat .dev_container) /bin/bash

### Stopping/saving from remote container
When you are done developing/testing (from the project directory on host machine) simply run:

    sh stop_dev_env.sh

This will save all project files to `MP_2/src`, while cleaning up the docker configuration files.

If you wish to save project files to another directory, simply pass to relative/absolute path as an arguement e.g.

    sh stop_dev_env.sh <path_to_save>

That's it! If anything is unclear, check any Dockerfile or shell script for documentation.

### Using your own environment 
Follow gRPC and CMake installation from the [gRPC docs](https://grpc.io/docs/languages/cpp/quickstart/). With some understanding of Dockerfiles, you could similarly replicate the `csce438/mp2:dev_env` container build in `./build_env/Dockerfile`.

### Compilation
Once you have a shell in the dev container run:

    cd /root/src
    make

This may take a while (especially if it's a fresh make). Then start the server (in the foreground, or detached followed by `&`):
    
    ./tsd -p <port>

Client(s)

    ./tsc -h <host> -p <port> -u <username>
    

## Other

<font size="1">have fun!</font>