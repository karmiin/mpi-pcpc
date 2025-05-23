#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // For isalnum, tolower, isspace

#define MAX_FILENAME_LEN 256
#define MAX_FILES 100
#define MAX_WORD_LEN 100
#define INITIAL_HIST_CAPACITY 64 // Initial capacity for histogram items

// MPI Message Tags
#define TAG_TASK 0
#define TAG_PROCESSED_FILE_ACK 0 // Worker to Master: done with file, ready for more
#define TAG_END_OF_TASKS_SEND_HISTOGRAM 1
#define TAG_HISTOGRAM_DATA_COUNT 2
#define TAG_HISTOGRAM_DATA_WORD 3
#define TAG_HISTOGRAM_DATA_FREQ 4
// Struct for word and its frequency
typedef struct {
    char word[MAX_WORD_LEN];
    int frequency;
} WordFreq;

// Struct for histogram
typedef struct {
    WordFreq* items;
    int count;      // Number of unique words
    int capacity;   // Allocated capacity of items array
} Histogram;

// Forward declarations for helper functions
void init_histogram(Histogram* hist);
void add_word_to_histogram(Histogram* hist, const char* word_str);
void merge_histograms(Histogram* dest_hist, const Histogram* source_hist);
void free_histogram_content(Histogram* hist);
int compare_wordfreq(const void* a, const void* b);
void sort_histogram_by_word(Histogram* hist);
void write_histogram_to_csv(const Histogram* hist, const char* csv_filename);
Histogram* count_words_in_file(const char* filename); // Modified to return Histogram*

// Initializes a histogram
void init_histogram(Histogram* hist) {
    hist->items = (WordFreq*)malloc(INITIAL_HIST_CAPACITY * sizeof(WordFreq));
    if (!hist->items) {
        perror("Failed to allocate histogram items");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    hist->count = 0;
    hist->capacity = INITIAL_HIST_CAPACITY;
}

// Ensures histogram has enough capacity
void ensure_capacity(Histogram* hist, int min_capacity) {
    if (hist->capacity < min_capacity) {
        int new_capacity = hist->capacity * 2;
        if (new_capacity < min_capacity) {
            new_capacity = min_capacity;
        }
        WordFreq* new_items = (WordFreq*)realloc(hist->items, new_capacity * sizeof(WordFreq));
        if (!new_items) {
            perror("Failed to reallocate histogram items");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        hist->items = new_items;
        hist->capacity = new_capacity;
    }
}

// Adds a word to the histogram or increments its frequency
void add_word_to_histogram(Histogram* hist, const char* word_str) {
    for (int i = 0; i < hist->count; ++i) {
        if (strncmp(hist->items[i].word, word_str, MAX_WORD_LEN) == 0) {
            hist->items[i].frequency++;
            return;
        }
    }
    // Word not found, add new entry
    ensure_capacity(hist, hist->count + 1);
    strncpy(hist->items[hist->count].word, word_str, MAX_WORD_LEN - 1);
    hist->items[hist->count].word[MAX_WORD_LEN - 1] = '\0'; // Ensure null termination
    hist->items[hist->count].frequency = 1;
    hist->count++;
}

// Merges source histogram into destination histogram
void merge_histograms(Histogram* dest_hist, const Histogram* source_hist) {
    for (int i = 0; i < source_hist->count; ++i) {
        const char* word = source_hist->items[i].word;
        int freq = source_hist->items[i].frequency;
        for (int k = 0; k < freq; ++k) { // Simple way to add frequency times
            add_word_to_histogram(dest_hist, word);
        }
    }
}

// Frees the content of a histogram (the items array)
void free_histogram_content(Histogram* hist) {
    if (hist && hist->items) {
        free(hist->items);
        hist->items = NULL;
        hist->count = 0;
        hist->capacity = 0;
    }
}

// Comparator for qsort (sorts by word alphabetically)
int compare_wordfreq(const void* a, const void* b) {
    WordFreq* wfA = (WordFreq*)a;
    WordFreq* wfB = (WordFreq*)b;
    return strncmp(wfA->word, wfB->word, MAX_WORD_LEN);
}

// Sorts the histogram by word
void sort_histogram_by_word(Histogram* hist) {
    if (hist && hist->count > 0) {
        qsort(hist->items, hist->count, sizeof(WordFreq), compare_wordfreq);
    }
}

// Writes histogram to a CSV file
void write_histogram_to_csv(const Histogram* hist, const char* csv_filename) {
    FILE* fp = fopen(csv_filename, "w");
    if (!fp) {
        perror("Errore nell'apertura del file CSV per la scrittura");
        return;
    }
    fprintf(fp, "word,frequency\n");
    for (int i = 0; i < hist->count; ++i) {
        fprintf(fp, "%s,%d\n", hist->items[i].word, hist->items[i].frequency);
    }
    fclose(fp);
}

// Funzione per contare le parole in un file e restituire un istogramma
Histogram* count_words_in_file(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        return NULL; // Return NULL if file can't be opened
    }

    Histogram* hist = (Histogram*)malloc(sizeof(Histogram));
    if (!hist) {
        perror("Failed to allocate histogram for file");
        fclose(fp);
        MPI_Abort(MPI_COMM_WORLD, 1); // Critical error
    }
    init_histogram(hist);

    char current_word[MAX_WORD_LEN];
    int char_idx = 0;
    int c;

    while ((c = fgetc(fp)) != EOF) {
        if (isalnum(c)) { // Consider only alphanumeric characters for words
            if (char_idx < MAX_WORD_LEN - 1) {
                current_word[char_idx++] = tolower(c); // Store lowercase
            }
        } else { // Separator found
            if (char_idx > 0) { // If a word was accumulated
                current_word[char_idx] = '\0';
                add_word_to_histogram(hist, current_word);
                char_idx = 0; // Reset for next word
            }
        }
    }
    // Handle last word if file doesn't end with a separator
    if (char_idx > 0) {
        current_word[char_idx] = '\0';
        add_word_to_histogram(hist, current_word);
    }
    fclose(fp);
    return hist;
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (rank == 0) { // MASTER process
        char file_list[MAX_FILES][MAX_FILENAME_LEN];
        int total_files = 0;

        FILE* fileListFile = fopen("filelist.txt", "r");
        if (fileListFile == NULL) {
            printf("Errore nell'apertura di filelist.txt\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        while (fgets(file_list[total_files], MAX_FILENAME_LEN, fileListFile) && total_files < MAX_FILES) {
            file_list[total_files][strcspn(file_list[total_files], "\n")] = '\0';
            file_list[total_files][strcspn(file_list[total_files], "\r")] = '\0';
            if (strlen(file_list[total_files]) > 0) { // Only add non-empty lines
                total_files++;
            }
        }
        fclose(fileListFile);

        Histogram global_histogram;
        init_histogram(&global_histogram);

        if (size == 1) { // Single process mode
            printf("Master: Running in single process mode.\n");
            if (total_files == 0) {
                printf("Master: No files to process.\n");
            }
            for (int i = 0; i < total_files; ++i) {
                Histogram* file_hist = count_words_in_file(file_list[i]);
                if (file_hist) {
                    merge_histograms(&global_histogram, file_hist);
                    free_histogram_content(file_hist);
                    free(file_hist);
                } else {
                    printf("Master: Could not process file %s\n", file_list[i]);
                }
            }
        } else { // MPI mode (size > 1)
            int num_workers = size - 1;
            int next_file_idx = 0;
            int workers_finished_and_sent_histograms = 0;
            MPI_Status status;

            if (total_files == 0) {
                printf("Master: No files to process. Signaling workers to terminate.\n");
            }

            for (int worker_rank = 1; worker_rank <= num_workers; ++worker_rank) {
                if (next_file_idx < total_files) {
                    MPI_Send(file_list[next_file_idx], MAX_FILENAME_LEN, MPI_CHAR, worker_rank, TAG_TASK, MPI_COMM_WORLD);
                    next_file_idx++;
                } else {
                    MPI_Send("", 1, MPI_CHAR, worker_rank, TAG_END_OF_TASKS_SEND_HISTOGRAM, MPI_COMM_WORLD);
                }
            }

            while (workers_finished_and_sent_histograms < num_workers) {
                int dummy_ack;
                MPI_Recv(&dummy_ack, 1, MPI_INT, MPI_ANY_SOURCE, TAG_PROCESSED_FILE_ACK, MPI_COMM_WORLD, &status);
                int sender_rank = status.MPI_SOURCE;

                if (next_file_idx < total_files) {
                    MPI_Send(file_list[next_file_idx], MAX_FILENAME_LEN, MPI_CHAR, sender_rank, TAG_TASK, MPI_COMM_WORLD);
                    next_file_idx++;
                } else {
                    MPI_Send("", 1, MPI_CHAR, sender_rank, TAG_END_OF_TASKS_SEND_HISTOGRAM, MPI_COMM_WORLD);

                    int num_unique_words;
                    MPI_Recv(&num_unique_words, 1, MPI_INT, sender_rank, TAG_HISTOGRAM_DATA_COUNT, MPI_COMM_WORLD, &status);

                    if (num_unique_words > 0) {
                        Histogram received_hist;
                        received_hist.items = (WordFreq*)malloc(num_unique_words * sizeof(WordFreq));
                        if (!received_hist.items) {
                            perror("Master failed to allocate for received histogram");
                            MPI_Abort(MPI_COMM_WORLD, 1);
                        }
                        received_hist.count = num_unique_words;
                        received_hist.capacity = num_unique_words;

                        for (int i = 0; i < num_unique_words; ++i) {
                            MPI_Recv(received_hist.items[i].word, MAX_WORD_LEN, MPI_CHAR, sender_rank, TAG_HISTOGRAM_DATA_WORD, MPI_COMM_WORLD, &status);
                            MPI_Recv(&received_hist.items[i].frequency, 1, MPI_INT, sender_rank, TAG_HISTOGRAM_DATA_FREQ, MPI_COMM_WORLD, &status);
                        }
                        merge_histograms(&global_histogram, &received_hist);
                        free(received_hist.items);
                    }
                    workers_finished_and_sent_histograms++;
                }
            }
        }

        printf("Master: Global histogram contains %d unique words.\n", global_histogram.count);
        sort_histogram_by_word(&global_histogram);
        write_histogram_to_csv(&global_histogram, "word_frequencies.csv");
        printf("Master: Output written to word_frequencies.csv\n");

        free_histogram_content(&global_histogram);

    } else { // WORKER process
        Histogram local_histogram;
        init_histogram(&local_histogram);
        MPI_Status status;

        while (1) {
            char task_filename[MAX_FILENAME_LEN];
            MPI_Recv(task_filename, MAX_FILENAME_LEN, MPI_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

            if (status.MPI_TAG == TAG_END_OF_TASKS_SEND_HISTOGRAM) {
                MPI_Send(&local_histogram.count, 1, MPI_INT, 0, TAG_HISTOGRAM_DATA_COUNT, MPI_COMM_WORLD);
                for (int i = 0; i < local_histogram.count; ++i) {
                    MPI_Send(local_histogram.items[i].word, MAX_WORD_LEN, MPI_CHAR, 0, TAG_HISTOGRAM_DATA_WORD, MPI_COMM_WORLD);
                    MPI_Send(&local_histogram.items[i].frequency, 1, MPI_INT, 0, TAG_HISTOGRAM_DATA_FREQ, MPI_COMM_WORLD);
                }
                break;
            }

            Histogram* file_hist = count_words_in_file(task_filename);
            if (file_hist) {
                merge_histograms(&local_histogram, file_hist);
                free_histogram_content(file_hist);
                free(file_hist);
            }

            int dummy_ack = rank;
            MPI_Send(&dummy_ack, 1, MPI_INT, 0, TAG_PROCESSED_FILE_ACK, MPI_COMM_WORLD);
        }
        free_histogram_content(&local_histogram);
    }

    MPI_Finalize();
    return 0;
}