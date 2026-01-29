## Assignment 2 Questions

#### Directions
Please answer the following questions and submit in your repo for the second assignment. Please keep the answers as short and concise as possible.

1. In this assignment I asked you provide an implementation for the `get_student(...)` function because I think it improves the overall design of the database application. After you implemented your solution do you agree that externalizing `get_student(...)` into it's own function is a good design strategy? Briefly describe why or why not.

   > **Answer**: Pulling it into a helper made the code cleaner—seeking, bounds checks, and error handling now live in one place instead of being duplicated in each command handler. That keeps things easier to test and change later (single responsibility), and callers only deal with a simple return code rather than raw I/O details.

2. Another interesting aspect of the `get_student(...)` function is how its function prototype requires the caller to provide the storage for the `student_t` structure:

   ```c
   int get_student(int fd, int id, student_t *s);
   ```

   Notice that the last parameter is a pointer to storage **provided by the caller** to be used by this function to populate information about the desired student that is queried from the database file. This is a common convention (called pass-by-reference) in the `C` programming language. 

   In other programming languages an approach like the one shown below would be more idiomatic for creating a function like `get_student()` (specifically the storage is provided by the `get_student(...)` function itself):

   ```c
   //Lookup student from the database
   // IF FOUND: return pointer to student data
   // IF NOT FOUND: return NULL
   student_t *get_student(int fd, int id){
       student_t student;
       bool student_found = false;
       
       //code that looks for the student and if
       //found populates the student structure
       //The found_student variable will be set
       //to true if the student is in the database
       //or false otherwise.

       if (student_found)
           return &student;
       else
           return NULL;
   }
   ```
   Can you think of any reason why the above implementation would be a **very bad idea** using the C programming language? Specifically, address why the above code introduces a subtle bug that could be hard to identify at runtime? 

   > **ANSWER:** That returns a pointer to `student`, which lives on the stack. As soon as the function returns the stack frame is reclaimed, so the caller would be using a dangling pointer—data might look okay sometimes and then randomly break later (classic undefined behavior from returning the address of a local).

3. Another way the `get_student(...)` function could be implemented is as follows:

   ```c
   //Lookup student from the database
   // IF FOUND: return pointer to student data
   // IF NOT FOUND or memory allocation error: return NULL
   student_t *get_student(int fd, int id){
       student_t *pstudent;
       bool student_found = false;

       pstudent = malloc(sizeof(student_t));
       if (pstudent == NULL)
           return NULL;
       
       //code that looks for the student and if
       //found populates the student structure
       //The found_student variable will be set
       //to true if the student is in the database
       //or false otherwise.

       if (student_found){
           return pstudent;
       }
       else {
           free(pstudent);
           return NULL;
       }
   }
   ```
   In this implementation the storage for the student record is allocated on the heap using `malloc()` and passed back to the caller when the function returns. What do you think about this alternative implementation of `get_student(...)`? Address in your answer why it work work, but also think about any potential problems it could cause.  
   
   > **ANSWER:** It “works” because the memory survives the return, but every successful call now forces the caller to `free` it. Forgetting to free leaks; ownership confusion could even cause double‑free; and the extra allocation per lookup is slower and can fragment the heap. Passing in caller‑owned storage avoids that overhead and makes ownership obvious.

4. Lets take a look at how storage is managed for our simple database. Recall that all student records are stored on disk using the layout of the `student_t` structure (which has a size of 64 bytes). Lets start with a fresh database by deleting the `student.db` file using the command `rm ./student.db`. Now that we have an empty database lets add a few students and see what is happening under the covers. Consider the following sequence of commands:

   ```bash
   > ./sdbsc -a 1 john doe 345
   > ls -l ./student.db
       -rw-r----- 1 bsm23 bsm23 128 Jan 17 10:01 ./student.db
   > du -h ./student.db
       4.0K    ./student.db
   > ./sdbsc -a 3 jane doe 390
   > ls -l ./student.db
       -rw-r----- 1 bsm23 bsm23 256 Jan 17 10:02 ./student.db
   > du -h ./student.db
       4.0K    ./student.db
   > ./sdbsc -a 63 jim doe 285 
   > du -h ./student.db
       4.0K    ./student.db
   > ./sdbsc -a 64 janet doe 310
   > du -h ./student.db
       8.0K    ./student.db
   > ls -l ./student.db
       -rw-r----- 1 bsm23 bsm23 4160 Jan 17 10:03 ./student.db
   ```

   For this question I am asking you to perform some online research to investigate why there is a difference between the size of the file reported by the `ls` command and the actual storage used on the disk reported by the `du` command. Understanding why this happens by design is important since all good systems programmers need to understand things like how linux creates sparse files, and how linux physically stores data on disk using fixed block sizes. Some good google searches to get you started: _"lseek syscall holes and sparse files"_, and _"linux file system blocks"_. After you do some research please answer the following:

   - Please explain why the file size reported by the `ls` command was 128 bytes after adding student with ID=1, 256 after adding student with ID=3, and 4160 after adding the student with ID=64? 

     > **ANSWER:** The code seeks to `id * sizeof(student_t)` before writing, so the logical end of file becomes `(id * 64) + 64`. For ID 1 that’s 128 bytes, for ID 3 it’s 256 bytes, and for ID 64 it’s 4160 bytes. `ls` shows that logical size stored in the inode even if the gaps (holes) were never written.

   - Why did the total storage used on the disk remain unchanged when we added the student with ID=1, ID=3, and ID=63, but increased from 4K to 8K when we added the student with ID=64? 

     > **ANSWER:** ext4 allocates disk in 4 KiB blocks by default. Records with IDs below 64 live inside the first 4096 bytes, so they fit in one block and `du` stays at 4K. The write for ID 64 crosses the 4 KiB boundary, so the filesystem has to allocate a second block; `du` jumps to 8K even though the apparent size grew a lot more.

   - Now lets add one more student with a large student ID number and see what happens:

     ```bash
     > ./sdbsc -a 99999 big dude 205 
     > ls -l ./student.db
     -rw-r----- 1 bsm23 bsm23 6400000 Jan 17 10:28 ./student.db
     > du -h ./student.db
     12K     ./student.db
     ```
     We see from above adding a student with a very large student ID (ID=99999) increased the file size to 6400000 as shown by `ls` but the raw storage only increased to 12K as reported by `du`. Can provide some insight into why this happened?

     > **ANSWER:** Adding ID 99999 seeks almost 6.4 MB into the file and writes one 64‑byte record. That makes a huge hole—only the blocks that actually contain data (the first block, the block with that record, and maybe a metadata block) are allocated, so `du` reports about 12K of real disk use while `ls` reports the full logical size of the sparse file.
