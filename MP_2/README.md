# CSCE438 MP2 — <font size="3">tiny</font> Social Networking Service

## Compilation & Use
This project is intended for deployment on an amazonlinux environment with Google's gRPC and Protocol Buffers installed. We're using Docker to build/deploy this environment which allows arbitrary resource allocation and platform independance in development, but use with Amazon's Cloud9 is also supported.

### Building Docker image

After ensuring Docker is properly installed, navigate to your `$WORKING_DIR`, `git clone` these files, then run:
    
    cd /CSCE438-MachineProblems/MP_2/build_env
    docker build -t csce438/mp2:build_env .

This may take a while, but it will pull an amazonlinux image and install all project dependancies. Keep the `csce438/mp2:build_env` image on hand, as we will use Docker caching to quickly spin up dev containers. 

### Starting Docker container for development/testing
Next spin up the dev container by running

    # After cloning repo, you may have to: chmod +x *_dev_env.sh 
    cd ..
    sh start_dev_env.sh

This will build the dev container (with all project files) and save the container's hash to `.dev_container` so we can automate the save/shutdown procedure.

In the VSCode window that opens automatically navigate to the "Remote Explorer" tab and remote into `csce438/mp2:dev`. 

Finally, install any VSCode extensions. Copy the remote's `.vscode-server` file to `./MP_2/.vscode-server` to keep you extensions persistant. Use your own method, or something like this should work:

    docker cp $(cat .dev_container):~/.vscode-server .

Open as many shells on the dev environment as you need by opening a new bash/zsh instance, and running:

    docker exec -it $(cat .dev_container) /bin/bash

### Stopping/saving from remote container
When you are done developing/testing, open `MP_2/stop_dev_env.sh` and set `$PROJ_PATH`, or pass your own path, e.g.:

    # If you updated PROJ_PATH
    sh stop_dev_env.sh

    # You can also pass a path as an argument
    sh stop_dev_env.sh <path_to_save>

This will save all project files to `MP_2/src`, while cleaning up the docker configuration files.

That's it! If anything is unclear, check any Dockerfile or shell script for documentation.

### Using your own environment 
Follow gRPC and CMake installation from the [gRPC docs](https://grpc.io/docs/languages/cpp/quickstart/). With some understanding of Dockerfiles, you could similarly replicate the `csce438/mp2:build_env` container build in `./build_env/Dockerfile`.

### Compilation
Once you have a shell in the dev container run:

    cd /root/src
    make

This may take a while (especially if it's a fresh make). Then start the server:
    
    # Run in the foreground
    ./tsd -p <port>

    # Run detached
    ./tsd -p <port> &

Client(s)

    ./tsc -h <host> -p <port> -u <username>
    
## Succinct System Design
https://docs.google.com/viewer?url=https://raw.githubusercontent.com/gkweston/CSCE438-MachineProblems/main/MP_2/MP2%20Design.pdf

## Other
A hackier step (than might be necessary) we're using is `docker cp`-ing all our project and `.vscode-server` files. This allows us to keep VSCode extensions on the non-persistant dev container.

A cleaner way of doing this might be to mount host machine files to the remote, but I didn't determine if this ports to both MacOS and Windows. The current solution is to use `*_dev_env.sh` which seems to work fine on Linux/MacOS/WSL2.

<font size="1">...have fun!</font>