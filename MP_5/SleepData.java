import java.util.Arrays;
import java.util.List;
import java.lang.Iterable;
import java.util.Iterator;
import java.io.IOException;
import java.lang.StringBuilder;
import java.io.DataInput;
import java.io.DataOutput;

import org.apache.hadoop.mapreduce.Mapper;
import org.apache.hadoop.mapreduce.Reducer;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.io.WritableComparable;
import org.apache.hadoop.io.IntWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.lib.output.FileOutputFormat;
import org.apache.hadoop.mapreduce.lib.input.TextInputFormat;
import org.apache.hadoop.io.LongWritable;

public class SleepData {
    public static class TokenizerMapper extends Mapper<Object, Text, Text, IntWritable>{

        private final static IntWritable one = new IntWritable(1);
        private Text word = new Text();

        /* Map Method */
        public void map(Object key, Text value, Context context) throws IOException, InterruptedException {
            // * Get each line of the tweet and make iterator
            List<String> lines = Arrays.asList(value.toString().split("\n"));
            String timestamp_h;
            boolean hasSleep = false;
            Iterator<String> itr = lines.iterator();

            // * Add to map if line representing tweet contains `sleep`
            while(itr.hasNext()) {
                // * Skip empty lines
                String line = itr.next();
                if (line.length() < 1){
                    continue;
                }

                // * Extract hour from timestamp
                if (line.charAt(0) == 'T') {
                    String[] time = line.split("\\s+");
                    timestamp_h = time[2].substring(0, 2);
                    word.set(timestamp_h);
                } else if (line.charAt(0) == 'W') {
                    List<String> tweet_tokens = Arrays.asList(line.split("\\s+"));
                    if (tweet_tokens.contains("sleep")) {
                        // * Break after 1 occurence of sleep is found
                        hasSleep = true;
                        break;
                    }
                }
            }
            value.set(key.toString());
            if (hasSleep) {
                context.write(word, one);
            }
        }
    }
    /* Reduce Method */
    public static class IntSumReducer extends Reducer<Text,IntWritable,Text,IntWritable> {
        private IntWritable result = new IntWritable();

        public void reduce(Text key, Iterable<IntWritable> values, Context context) throws IOException, InterruptedException {
            int sum = 0;
            for (IntWritable val : values) {
                sum += val.get();
            }
            result.set(sum);
            context.write(key, result);
        }
    }

    public static void main(String[] args) throws Exception {
        Configuration conf = new Configuration();

        // * Set the conf delimiter to an empty line to split data
        conf.set("textinputformat.record.delimiter", "\n\n");

        // Set job as `sleepdata`
        Job job = Job.getInstance(conf, "sleepdata");

        // * Boilerplate
        job.setInputFormatClass(TextInputFormat.class);
        job.setJarByClass(SleepData.class);
        job.setMapperClass(TokenizerMapper.class);
        job.setCombinerClass(IntSumReducer.class);
        job.setReducerClass(IntSumReducer.class);
        job.setOutputKeyClass(Text.class);
        job.setOutputValueClass(IntWritable.class);
        FileInputFormat.addInputPath(job, new Path(args[0]));
        FileOutputFormat.setOutputPath(job, new Path(args[1]));
        System.exit(job.waitForCompletion(true) ? 0 : 1);
    }
}
