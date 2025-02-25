#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define MAX_PEERS 100

typedef struct
{
    char filename[MAX_FILENAME];
    int num_peers;
    int peers[MAX_PEERS]; // Lista de ranks care au segmente din acest fisier
} SwarmEntry;

typedef struct {
    int rank;
    int request_count;
} PeerRequestInfo;

SwarmEntry swarm[MAX_FILES];
int swarm_size = 0;

typedef struct
{
    char filename[MAX_FILENAME];
    int num_segments;
    char hashes[MAX_CHUNKS + 1][HASH_SIZE + 1];
} FileInfo;

typedef struct {
    int num_owned_files;
    FileInfo owned_files[MAX_FILES];
    int num_desired_files;
    char desired_files[MAX_FILES][MAX_FILENAME];
    pthread_mutex_t mutex; // Mutex pentru sincronizarea threadului de download cu cel de upload
} PeerInfo;


typedef struct {
    int rank;
    PeerInfo *peer_info;
} ThreadArgs;

PeerInfo peer_info_list[MAX_PEERS + 1];

// Gaseste un fisier in swarm sau adauga unul nou
int find_or_add_file(const char *filename)
{
    for (int i = 0; i < swarm_size; ++i) {
        if (strcmp(swarm[i].filename, filename) == 0) {
            return i;
        }
    }
    // Cazul in care nu exista fisier nou
    strncpy(swarm[swarm_size].filename, filename, MAX_FILENAME);
    swarm[swarm_size].num_peers = 0;
    return swarm_size++;
}

// Citirea fisierului de intrare
PeerInfo read_input_file(int rank)
{
    char filename[20];
    snprintf(filename, sizeof(filename), "in%d.txt", rank);

    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Eroare la deschiderea fișierului %s\n", filename);
        exit(-1);
    }

    PeerInfo peer_info;
    fscanf(file, "%d", &peer_info.num_owned_files);

    for (int i = 0; i < peer_info.num_owned_files; ++i) {
        fscanf(file, "%s %d", peer_info.owned_files[i].filename, &peer_info.owned_files[i].num_segments);
        for (int j = 0; j < peer_info.owned_files[i].num_segments; ++j) {
            fscanf(file, "%s", peer_info.owned_files[i].hashes[j]);
        }
    }

    fscanf(file, "%d", &peer_info.num_desired_files);
    for (int i = 0; i < peer_info.num_desired_files; ++i) {
        fscanf(file, "%s", peer_info.desired_files[i]);
    }

    fclose(file);
    return peer_info;
}

int find_or_add_peer(int rank, int *total_peers, PeerRequestInfo *peer_requests) {
    //Caut peer
    for (int i = 0; i < *total_peers; ++i) {
        if (peer_requests[i].rank == rank) {
            return i;
        }
    }
    // Daca nu e, adaug un peer nou
    peer_requests[(*total_peers)].rank = rank;
    peer_requests[(*total_peers)].request_count = 0;
    return (*total_peers)++;
}

//Sortez swarmul pentru un fisier in functie de solicitarile pe care le au toti clientii
void sort_peers(SwarmEntry *entry, int *total_peers, PeerRequestInfo *peer_requests) {
    for (int i = 0; i < entry->num_peers - 1; ++i) {
        for (int j = i + 1; j < entry->num_peers; ++j) {
            int peer1_index = find_or_add_peer(entry->peers[i], total_peers, peer_requests);
            int peer2_index = find_or_add_peer(entry->peers[j], total_peers, peer_requests);
            if (peer_requests[peer1_index].request_count > peer_requests[peer2_index].request_count) {
                int temp = entry->peers[i];
                entry->peers[i] = entry->peers[j];
                entry->peers[j] = temp;
            }
        }
    }
}


void *download_thread_func(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    int rank = args->rank;
    PeerInfo *peer_info = args->peer_info;
    int request_count = 0;

    for (int i = 0; i < peer_info->num_desired_files; ++i) {
        char *desired_file = peer_info->desired_files[i];
        int num_peers;
        int peers[MAX_PEERS];
 
        //Se cere swarmul de la tracker
        MPI_Send(desired_file, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);
        MPI_Status status;
        MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(peers, num_peers, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD, &status);

        //Adaugarea de date in peer->info
        pthread_mutex_lock(&peer_info->mutex);
        FileInfo *new_file = &peer_info->owned_files[peer_info->num_owned_files];
        strncpy(new_file->filename, desired_file, MAX_FILENAME);
        new_file->num_segments = 0;
        peer_info->num_owned_files++;
        pthread_mutex_unlock(&peer_info->mutex);

        char file_path[50];
        snprintf(file_path, sizeof(file_path), "client%d_%s", rank, desired_file);
        FILE *output_file = fopen(file_path, "wb");
        if (!output_file) {
            fprintf(stderr, "Rank %d: Eroare la crearea fișierului %s\n", rank, file_path);
            continue;
        }

        for (int chunk = 0; chunk < MAX_CHUNKS; ++chunk) {
            char chunk_data[HASH_SIZE + 1];

            for (int j = 0; j < num_peers; ++j) {
                int peer = peers[j];
                //Se cer datele de la un alt client
                MPI_Send(&chunk, 1, MPI_INT, peer, 2, MPI_COMM_WORLD);
                MPI_Send(desired_file, MAX_FILENAME, MPI_CHAR, peer, 2, MPI_COMM_WORLD);

                MPI_Recv(chunk_data, HASH_SIZE + 1, MPI_CHAR, peer, 4, MPI_COMM_WORLD, &status);
                //Asteapta mesajul ACK
                char ack_message[4];
                MPI_Recv(ack_message, sizeof(ack_message), MPI_CHAR, peer, 5, MPI_COMM_WORLD, &status);

                //Se adauga datele in fisierul de iesire
                if (strlen(chunk_data) > 0) {
                    if (chunk == 0)
                        fprintf(output_file, "%s", chunk_data);
                    else
                        fprintf(output_file, "\n%s", chunk_data);

                    pthread_mutex_lock(&peer_info->mutex);
                    strncpy(new_file->hashes[chunk], chunk_data, HASH_SIZE);
                    new_file->num_segments++;
                    pthread_mutex_unlock(&peer_info->mutex);
                    break;
                }
            }
            request_count++;

            // Dupa fiecare 10 cereri, solicit o lista actualizata de peers
            if (request_count % 10 == 0) {
                MPI_Send(desired_file, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);
                MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD, &status);
                MPI_Recv(peers, num_peers, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD, &status);
            }
        }

        fclose(output_file);
    }

    MPI_Send("STOP", MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);
    return NULL;
}


void *upload_thread_func(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    PeerInfo *peer_info = args->peer_info;

    while (1) {
        MPI_Status status;
        int chunk;
        char requested_file[MAX_FILENAME];

        MPI_Recv(&chunk, 1, MPI_INT, MPI_ANY_SOURCE, 2, MPI_COMM_WORLD, &status);
        if (chunk == 200)
            break; //Acesta e semnalul de la tracker ca trebuie sa se opreasca
        MPI_Recv(requested_file, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD, &status);

        char chunk_data[HASH_SIZE + 1] = "";

        //Accesarea datelor pe care le are clientul cu sincronizare
        pthread_mutex_lock(&peer_info->mutex);
        for (int i = 0; i < peer_info->num_owned_files; ++i) {
            if (strcmp(peer_info->owned_files[i].filename, requested_file) == 0) {
                if (chunk < peer_info->owned_files[i].num_segments) {
                    strncpy(chunk_data, peer_info->owned_files[i].hashes[chunk], HASH_SIZE);
                    chunk_data[HASH_SIZE] = '\0';
                }
                break;
            }
        }
        pthread_mutex_unlock(&peer_info->mutex);

        MPI_Send(chunk_data, HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, 4, MPI_COMM_WORLD);
        // Trimite un mesaj ACK pentru a confirma trimiterea chunk-ului
        char ack_message[] = "ACK";
        MPI_Send(ack_message, sizeof(ack_message), MPI_CHAR, status.MPI_SOURCE, 5, MPI_COMM_WORLD);
    }

    return NULL;
}

void tracker(int numtasks, int rank)
{
    int completed_downloads = 0;
    int num_clients = numtasks - 1;
    MPI_Status status;
    PeerRequestInfo peer_requests[MAX_PEERS];

    int total_peers = 0;
    //Primeste mesajele initiale de la clienti
    for (int i = 1; i <= num_clients; ++i) {
        int num_files;
        MPI_Recv(&num_files, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
        for (int j = 0; j < num_files; ++j) {
            char filename[MAX_FILENAME];
            int num_segments;
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, &status);
            MPI_Recv(&num_segments, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);

            //Adauga clientul in swarm-ul fisierului
            int file_index = find_or_add_file(filename);
            SwarmEntry *entry = &swarm[file_index];
            entry->peers[entry->num_peers++] = i;
            find_or_add_peer(i, &total_peers, peer_requests);
        }
    }

    // Trimite ACK fiecarui client
    for (int i = 1; i <= num_clients; ++i) {
        MPI_Send("ACK", 4, MPI_CHAR, i, 0, MPI_COMM_WORLD);
    }
    
    // Raspunde cererilor de la peers
    while (1) {
        char requested_file[MAX_FILENAME];
        int requesting_peer;
        MPI_Status status;

        MPI_Recv(requested_file, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        //Daca primeste stop de la un client il contorizeaza la completed_downloads
        if(strcmp(requested_file, "STOP") ==0) {
            completed_downloads++;
            if (completed_downloads == numtasks - 1) {
                for (int i = 1; i <= num_clients; ++i) {
                    int chunk = 200;
                    MPI_Send(&chunk, 1, MPI_INT, i, 2, MPI_COMM_WORLD);
                }
                break;
            } else {
                continue;
            }

        }
        requesting_peer = status.MPI_SOURCE;

        int file_index = find_or_add_file(requested_file);
        SwarmEntry *entry = &swarm[file_index];

        // Verific daca requesting_peer este deja in lista de peers
        int peer_exists = 0;
        for (int i = 0; i < entry->num_peers; ++i) {
            if (entry->peers[i] == requesting_peer) {
                peer_exists = 1;
                break;
            }
        }

        if (entry->num_peers > 0) {
            // Incrementam numarul de solicitari pentru primul peer din lista
            int first_peer_index = find_or_add_peer(entry->peers[0], &total_peers, peer_requests);
            peer_requests[first_peer_index].request_count++;
        }
        if (!peer_exists) {
            entry->peers[entry->num_peers++] = requesting_peer;
        }
        //Sortez swarmul pentru fisier in functie de numarul de solicitari
        sort_peers(entry, &total_peers, peer_requests);

        // Trimite lista de peers care detin fisierul
        MPI_Send(&entry->num_peers, 1, MPI_INT, requesting_peer, 1, MPI_COMM_WORLD);
        MPI_Send(entry->peers, entry->num_peers, MPI_INT, requesting_peer, 1, MPI_COMM_WORLD);
    }
}

void peer(int numtasks, int rank)
{
    PeerInfo peer_info = read_input_file(rank);
    peer_info_list[rank] = peer_info;

    //Se informeaza trackerul legat de fisierele detinute
    MPI_Send(&peer_info.num_owned_files, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
    for (int i = 0; i < peer_info.num_owned_files; ++i) {
        MPI_Send(peer_info.owned_files[i].filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(&peer_info.owned_files[i].num_segments, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
    }

    // Așteptarea confirmării ACK de la tracker
    char ack[4];
    MPI_Status thread_status;
    MPI_Recv(ack, 4, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, &thread_status);
    if (strcmp(ack, "ACK") == 0) {
        // e bine
    }
    ThreadArgs args = {rank, &peer_info};
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;
    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&args);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }
    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&args);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }
    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }
    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}

int main(int argc, char *argv[])
{
    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    }
    else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
