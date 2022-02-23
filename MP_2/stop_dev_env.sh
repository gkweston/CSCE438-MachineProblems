# Get the project path from user or use PROJ_PATH
PROJ_PATH="/Users/gkweston/Git/CSCE438-MachineProblems/MP_2/";

# Ensure we don't accidentally copy files, could be refactored
# Check for 2 args and -c flag
if [ $1 ] && [ $2 ] && [ $1 == "-c" ];
then
	PROJ_PATH=${2}

# Exit if ARG1 is not -c
elif [ $1 ] && [ $1 != "-c" ];
then
	echo "${0} requires 0 OR 2 args:";
	echo "${0} -c <path_to_save>";
	exit;

# Exit if 1 arg
elif [[ $1 == "-c" ]];
then
	echo "Custom path called as:";
	echo "${0} -c <path_to_save>";
	exit;
fi

echo "Saving to ${PROJ_PATH}";

# Check that we have .dev_container file on hand and it contains
# a valid docker container hash
DEV_CONTAINER="deadbeef"; # garbage value so we can catch errors
if test -f ".dev_container";
then
	DEV_CONTAINER=$(cat .dev_container);
else
	echo ".dev_container not set. Manually copy/stop container via:";
	echo "docker ps";
	echo "docker cp <container>:/root/build <path_to_save>";
	echo "docker stop <container>";
	exit;
fi

if [[ ${#DEV_CONTAINER} -eq 64 ]];
then
	# Copy files from remote to host
	docker cp ${DEV_CONTAINER}:/root/build $PROJ_PATH;
	echo "Build files saved.";
	
	# Stop development container
	echo "Stopping container...may take a moment...";
	docker stop ${DEV_CONTAINER};
	echo "Container stopped."

else
	echo ".dev_container corrupted value";
fi;

rm .dev_container;

