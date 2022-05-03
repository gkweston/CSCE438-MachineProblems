# CSCE438 MP5 — MapReduce on Hadoop File System
## Description
We're given a ~2.5GB text file of tweets delimited by "\n\n" which we use to count the time of day most tweets are made, along with the time of day most tweets with the word `sleep` are made.

## Compilation & Use
Set up you HDFS with the tweets in `/user/root/data`.

```bash
# Compile the source file, in this case SleepData.java
hadoop com.sun.tools.javac.Main SleepData.java

# Compress into a jar
jar -cvf SleepData.jar SleepData*.class

# Add and execute MapReduce jobs
hadoop jar SleepData.jar SleepData /user/root/data /user/root/output
```

In between runs, you can clear the `user/root/output` file via:
```bash
hdfs dfs -rm -r -skipTrash /user/root/output
```

That's it!
