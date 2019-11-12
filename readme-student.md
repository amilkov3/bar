# Project README file

This is **YOUR** Readme file.

**Note** - if you prefer, you may submit a PDF version of this readme file.  Simply create a file called **readme-student.pdf** in the same directory.  When you submit, one or the other (or both) of these files will be submitted.  This is entirely optional.

**Note: the `submit.py` script will only submit files named readme-student.md and readme-student.pdf**

## Project Description

Your README file is your opportunity to demonstrate to us that you understand the project.  Ideally, this
should be a guide that someone not familiar with the project could pick up and read and understand
what you did, and why you did it.

Specifically, we will evaluate your submission based upon:

- Your project design.  Pictures are particularly helpful here.
- Your explanation of the trade-offs that you considered, the choices you made, and _why_ you made those choices.
- A description of the flow of control within your submission. Pictures are helpful here.
- How you implemented your code. This should be a high level description, not a rehash of your code.
- How you _tested_ your code.  Remember, Bonnie isn't a test suite.  Tell us about the tests you developed.
  Explain any tests you _used_ but did not develop.
- References: this should be every bit of code that you used but did not write.  If you copy code from
  one part of the project to another, we expect you to document it. If you _read_ anything that helped you
  in your work, tell us what it was.  If you referred to someone else's code, you should tell us here.
  Thus, this would include articles on Stack Overflow, any repositories you referenced on github.com, any
  books you used, any manual pages you consulted.


In addition, you have an opportunity to earn extra credit.  To do so, we want to see something that
adds value to the project.  Observing that there is a problem or issue _is not enough_.  We want
something that is easily actioned.  Examples of this include:

- Suggestions for additional tests, along with an explanation of _how_ you envision it being tested
- Suggested revisions to the instructions, code, comments, etc.  It's not enough to say "I found
  this confusing" - we want you to tell us what you would have said _instead_.

While we do award extra credit, we do so sparingly.

### Design

I used the System V API for orchestrating shared memory.

#### Simplecached
Polls request queue, for requests from proxy server, and enqueues them to steque which is consumed by a set number of worker threads who are responsible for reading
from cache and transmitting response via shared memory. Each such request message contains the thread id of the thread that sent it, in the `mtype` field of the payloads of `msgrcv` and `msgsnd`. A worker attempts to get the file descriptor of the payload to transmit back via the provided `simplecache` API.
If such a file is present, it notifies the requesting process by sending a message to the response queue, again sending back the same thread id in `mtype` that originally sent the request. This message also contains the file size and the `gfstatus_t`, It then attaches a shared memory segment to its address space via `shmat` and begins reading chunks from the file and writing them to this segment. This needs to be synchronized in an adhoc wait via 2 semaphores, one that guards the requestor reading and another the worker writing. We lock the write semaphore while the worker writes as much of the file as will fit in the shared memory segment and then unlock the read semaphore to allow the proxy worker to read the chunk, proceeding until the entire file is transmitted 

#### Webproxy
At startup we initialized a steque of shared memory segments. And a copy of this steque if we receive a signal, so we can reclaim the memory of these segments. We also create and initialize worker threads to handle requests. When a request comes in, it its handled by a worker, which spins until the request queue comes online. It then pulls a shared memory segment off of the steque and sends a request to `simplecached`. It spins until the response queue is online and then receives the response to its request. If the response was a success, it sends a success header and proceeds to read the file contents from shared memory segment. Otherwise it sends a failure header and returns early. It locks the read semaphore and waits for `simplecached` to write to memory. It then reads the contents of the segment chunk by chunk sending a response back to the client. Once it's read the entire segment, it unlocks the write semaphore. This repeats until the entire file is transmitted. The memory segment is then detached and returned to the steque for other workers to use

#### Assumptions
* send `GF_ERROR` on all `4xx`. The README was vague here
* `Content-Length` returned by server is precise length of file

### Testing

Like project 1, I tested all the possible response statuses by modifying the contents of `workload.txt` and then running `gfclient_download`

### Suggestions for Improvement
Only one: Bonnie, which was arguably more painful than project 1. For my first submissions I noticed the logs were all `Truncated`. I read on Piazza that they're limited to 10kB so I commented out as much of my logs as I could whilst still being able to see what's actually going on and diagnose whatever was failing in the first place and resubmitted. Again `Truncated`. Why random, nonsensical roadblocks like this are artificially imposed I simply cannot conjure up a reasonable explanation. So after being unable to see anything other than `Truncated` displayed in the test output for most tests, failures or successes and anything remotely useful being logged in the others, and tracking the same banging-heads-against-the-wall posts on Piazza as those with project 1 around stuff like the client hung issues for example, I decided to settle for the progress I had made instead this time and turn in what I have. Again what this accomplishes in the way of pedagogy is beyond me. This is not even vaguely how real software development works and instead of providing useful hints and aids to give the student palpable pushes along to figuring out where and why a certain test is failing, this is just a pointless easter egg hunt where the student scrutinizes anyting that looks even vaguely wrong in their code, modifies it to see if the failing tests are affected in any way, only to have tests that weren't failing before fail, again having little or nothing to go off of in the test output to figure out what cause them, all the while tracking how many submissions they have left, is an exercise in futility and little else. Anyway I'll give the same suggestion I gave for project 1: at the very least provide the test binary as part of the project source, eliminating the charade and reserving Bonnie only for final submissions. But also if you're not going to provide the test source, then the log outputs have to be alot more illuminating than they are now. Also these random `Truncated` things are just an annoyance and rationing how many bytes a program outputs to stdout doesn't make much sense in the first place.

Other than that again another fantastic assignment not even the test suite could ruin. Nice work!



