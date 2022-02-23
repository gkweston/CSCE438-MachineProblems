# WARNING: This script will force docker pruning

# Start dev container with current files then save it's hash
# so for the shutdown sequence

# This should be fast as long as you have csce438/mp2:build_env
# on hand (docker caching). Otherwise, use another method.
docker build -t csce438/mp2:dev .;

# Save the container's hash to a file so we can stop it properly
echo $(docker run -itd csce438/mp2:dev /bin/bash) > .dev_container
echo "Housekeeping...";
docker container prune -f;
docker image prune -f;
echo "Done.";

# Run VSCode so user can remote in
code;
echo "You may now manually remote into csce438/mp2:dev";
