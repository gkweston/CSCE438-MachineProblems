# Start container mounted here
docker run --rm -itv $(pwd):/mnt/mp --name hadooper sequenceiq/hadoop-docker:2.7.0 /etc/bootstrap.sh -bash;
