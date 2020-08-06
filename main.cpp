#include "fat.h"
using namespace std;

vector<string> path;
DIR_HANDLER current_directory;
list<MFILE> openFiles;
list <DIR_HANDLER> openDirectories;
FILE* f;
struct boot_sector boot;
int openFilesID = 0;
// 0 zarezerowane dla roota
int currentIdDirectoryHandler = 1;

lba_t volume_start = 0;
lba_t fat1_start = 0;
lba_t fat2_start = 0;
lba_t root_start = 0;
lba_t sectors_per_root = 0;
lba_t data_start = 0;
lba_t available_clusters = 0;
uint64_t available_bytes = 0;
uint16_t* FAT = NULL;

// czyta z woluminu
size_t readblock(void* buffer, uint32_t first_block, size_t block_count) {
	if (buffer == NULL) return 0;
	if (fseek(f, first_block * SECTOR_SIZE, SEEK_SET)) return 0;
	return fread(buffer, SECTOR_SIZE, block_count, f);
}

//wczytuje boot 
int read_boot_sector() {
	return readblock(&boot, 0, 1);
}

// oblicza pomocne pozycje w woluminie 
void calculate_regions(void) {
	volume_start = 0;
	fat1_start = volume_start + boot.BIOS_Parameter_Block.reserved_sectors;
	fat2_start = fat1_start + boot.BIOS_Parameter_Block.sectors_per_fat;
	root_start = fat1_start + 2 * boot.BIOS_Parameter_Block.sectors_per_fat;
	sectors_per_root = (boot.BIOS_Parameter_Block.root_dir_capacity * sizeof(directory_entry)) / boot.BIOS_Parameter_Block.bytes_per_sector;
	data_start = root_start + sectors_per_root;
	available_clusters = (boot.BIOS_Parameter_Block.logical_sectors16 - boot.BIOS_Parameter_Block.reserved_sectors - 2 * boot.BIOS_Parameter_Block.sectors_per_fat - sectors_per_root) / boot.BIOS_Parameter_Block.sectors_per_cluster;
	available_bytes = available_clusters * boot.BIOS_Parameter_Block.sectors_per_cluster * boot.BIOS_Parameter_Block.bytes_per_sector;
	return;
}

// zwrca numer klastra w tablicy danych, lub -1 w razie bledu lub jesli nie ma dlaszego
lba_t n_cluster_location(uint64_t ncluster) {
	// pierwszy kalsgter ma indeks 2 nie zero
	return data_start + (ncluster - 2) * boot.BIOS_Parameter_Block.sectors_per_cluster;
}

// zwraca nastepny dostepny klaster dla danego pliku lub -1 jesli nie ma dlaszych klastrow
int read_next_cluster_FAT(uint16_t cluster) {
	if (cluster < 0) return -1;
	if (*(FAT + cluster) != EOC) return *(FAT + cluster);
	return -1;
}

// inicializuje katalog wejsciowy
void read_root_directory() {

	uint8_t sector[SECTOR_SIZE] = { 0 };
	MDIR* dir;
	bool endRootDirectoryEntries = false;
	DIR_HANDLER d;
	d.id = 0;

	while (1) {
		readblock(sector, root_start, 1);

		for (int i = 0; i < (int)((SECTOR_SIZE / sizeof(MDIR)) ); i++) {
			dir = (MDIR*)(sector + (i * sizeof(MDIR)));
			if (dir->filename[0] == 0) {
				endRootDirectoryEntries = true;
				break;
			}
			d.directory_list.push_back(*dir);
		}
		if (endRootDirectoryEntries) break;
	}

	openDirectories.push_back(d);
	current_directory = d;
	path.push_back("ROOT");
}

// wywwietla aktualna sciezke
void printPath() {
	for (string v : path) {
		cout << v << "/";
	}
}

// oblicza mdlogisc nazwy katalogu
int calculateDirlength(char* buff) {
	if (buff == NULL) return -1;
	int l = 0;
	for (int i = 0; i < 8; i++) {
		if (isalpha(buff[i]) != 0 || isdigit(buff[i]) != 0) {
			l++;
		}
	}
	return l;
}

// otwiera katlog w LOKALNEJ lokacji i dodaje go na liste katalogow otwartych
int openDirectory(char* name) {
	if (name == NULL) return -18;
	bool find = false;
	DIR_HANDLER dirh;
	uint8_t sector[SECTOR_SIZE * boot.BIOS_Parameter_Block.sectors_per_cluster] = { 0 };
	MDIR* dir;

	for (MDIR d : current_directory.directory_list) {
		if ((strncmp(name, d.filename, calculateDirlength(d.filename)) == 0) && (calculateDirlength(d.filename) == (int)strlen(name))) {
			if (d.attribute_byte != DIRECTORY) return -1;
			int cur = d.starting_cluster;

			while (1) {
				readblock(sector, n_cluster_location(cur), boot.BIOS_Parameter_Block.sectors_per_cluster);
				for (int i = 0; i < (int)(((boot.BIOS_Parameter_Block.sectors_per_cluster * SECTOR_SIZE) / sizeof(MDIR))); i++) {
					dir = (MDIR*)(sector + (i * sizeof(MDIR)));
					if (dir->filename[0] == '\0') break;
					dirh.directory_list.push_back(*dir);
				}

				cur = read_next_cluster_FAT(cur);
				if (cur == -1) break;
			}
			find = true;
		}
	}

	if (find == false) {
		return -1;
	}

	currentIdDirectoryHandler++;
	dirh.id = currentIdDirectoryHandler;
	openDirectories.push_back(dirh);

	return currentIdDirectoryHandler;
}

// zamyka otawrty katalog (usuwa go z listy katalogow otwartych)
int closeDirectory(int ID) {
	if (ID < 0) return -1;
	int res = -1;

	for (auto it = openDirectories.begin(); it != openDirectories.end(); ) {

		if ((it)->id == ID) {
			it = openDirectories.erase(it);
			res = 0;
		}
		else {
			++it;
		}
	}

	return res;
}

// funckja pomocnicza do readDirectory, wyswietla typ, rozmiar nazwe etc.
void printDirectory(MDIR* d) {
	if (d->filename[0] == '.') return;

	printf("%02d/%02d/%d", month_day(d->creation_data), month(d->creation_data), year(d->creation_data));
	printf(" %02d:%02d", hours(d->creation_time), minutes(d->creation_time));
	if (d->attribute_byte == DIRECTORY) {
		printf(" <DIRECTORY> ");
	}
	else {
		printf(" %11d ", d->file_size);
	}

	for (int i = 0; (i < 8) && (isdigit(d->filename[i]) != 0 || isalpha(d->filename[i]) != 0); i++) {
		printf("%c", d->filename[i]);
	}

	if (d->attribute_byte != DIRECTORY) {
		printf(".");
		for (int i = 0; i < 3; i++) printf("%c", d->filename_extension[i]);
	}

	printf("\n");
}

// czyta katalog na podstawie jego ID, z listy katalogow otwartych
int readDirectory(int ID) {
	if (ID < 0) return -1;

	for (DIR_HANDLER dirh : openDirectories) {
		if (dirh.id == ID) {
			for (MDIR d : dirh.directory_list) {
				printDirectory(&d);
			}
		}
	}

	return 0;
}

// zmiania aktualny katalog na katlaog podny NAZWA, lub do katalogu nadrzednego " .."
int cd(char* name) {
	if (name == NULL) return -1;

	int cur = current_directory.id;


	if (strcmp(name, "..") == 0) {

		if (openDirectories.size() == 2) {
			closeDirectory(cur);
			current_directory = openDirectories.front();
			path.pop_back();
		}
		else if (openDirectories.size() == 1) {
			printf("Jestes w katalogu glownym !!!\n\n");
		}
		else {
			closeDirectory(cur);
			current_directory = openDirectories.back();
			path.pop_back();
		}

		return 0;
	}

	int er = openDirectory(name);
	if (er == -1) {
		printf("Katalog o podanej nazwie nie istnieje !!!\n\n");
		return -1;
	}

	string n(name);
	path.push_back(n);

	for (DIR_HANDLER d : openDirectories) {
		if (er == d.id) {
			current_directory = d;
		}
	}
	return 0;
}

// sprawdza poprawnosc podanej nazwy pliku, czy zawiera rozszerzenie etc.
int fileNameValidate(char* fileName) {
	if (fileName == NULL) return -1;
	int len = strlen(fileName);
	bool dot = false;
	if (len < 5 || len > 12) return -1;
	if (fileName[0] == '.') return -1;
	for (int i = 0; i < len; i++) {
		if (fileName[i] == '.') dot = true;
	}
	if (dot == false) return -1;
	return 0;
}

// otwiera plik o nazwie name z aktualnego katalogu,
// daje go na liste plikow otwartych i zwraca do niego id 
int openFile(char* name) {
	if (name == NULL || fileNameValidate(name) != 0) return -1;

	char fname[12] = { 0 };
	strcpy(fname, name);
	char* fileName;
	char* fileExt;
	fileName = strtok(fname, ".");
	fileExt = strtok(NULL, ".");
	MFILE f;
	f.ID = -1;

	for (MDIR d : current_directory.directory_list) {
		if (strncmp(fileName, d.filename, strlen(fileName)) == 0) {
			if (strncmp(fileExt, d.filename_extension, strlen(fileExt)) == 0) {
				f.file = d;
				f.ID = openFilesID;
				f.current_cluster = d.starting_cluster;
				f.pos = d.file_size;
				openFiles.push_back(f);
				openFilesID++;
				break;
			}
		}
	}

	return f.ID;
}

// zamyka plik podany ID (usuwa go z list plikow otwartych)
int closeFile(int ID) {
	if (ID < 0) return -1;
	int res = -1;

	for (auto it = openFiles.begin(); it != openFiles.end(); ) {

		if ((it)->ID == ID) {
			it = openFiles.erase(it);
			res = 0;
		}
		else {
			++it;
		}
	}

	return res;
}

// czyta plik na podstawie ID, czyta cały klaster dla danego pliku
// wynik zapisuje w buff, zwraca liczbe odczytancyh bajtow lub
// -1 w razie bledu lub konca dlaeszeko klastra
int readFile(int ID, void* buff) {
	if (ID < 0 || buff == NULL) return -1;
	int read = 0;

	for (list<MFILE>::iterator fh = openFiles.begin(); fh != openFiles.end(); fh++) {
		if (fh->ID == ID) {


			if (fh->current_cluster == -1) {
				return -1;
			}

			readblock(buff, n_cluster_location(fh->current_cluster), 2);
			fh->current_cluster = read_next_cluster_FAT(fh->current_cluster);

			if (fh->pos >= (2 * SECTOR_SIZE)) {
				read = 2 * SECTOR_SIZE;
				fh->pos = fh->pos - 2 * SECTOR_SIZE;
			}
			else {
				read = fh->pos;
			}

			break;
		}
	}
	return read;
}

void rootinfo() {
	DIR_HANDLER d = openDirectories.front();
	int numerOfEntries = d.directory_list.size();
	int maxEntries = boot.BIOS_Parameter_Block.root_dir_capacity;
	printf("Liczba wpisow w katalogu glownym : %d\n", numerOfEntries);
	printf("Maksymalna liczba wpisow w katalogu glownym : %d\n", maxEntries);
	printf("Procentowe wypelnienie katalogu glownego : %f %%\n", (double)numerOfEntries / (maxEntries));
}

void spaceinfo() {

	int used = 0, notUsed = 0, dmgd = 0, end = 0, size = (boot.BIOS_Parameter_Block.sectors_per_fat * SECTOR_SIZE) / sizeof(uint16_t);
    uint16_t FAT_COPY[size];
    memcpy(FAT_COPY,FAT,size * sizeof(uint16_t));
    int startCluster = 0;
    
	for (int i = 2; i < size; i++) {
        startCluster = i;
        if((FAT_COPY[i] >= 0x0002 && FAT_COPY[i] <= 0xFFF6) || (FAT_COPY[i] >= 0xFFF8 && FAT_COPY[i] <= 0xFFFF)){
            int numberOfClusters = 1;
			int startCluster = i;
			while (1) {
                FAT_COPY[startCluster] = 0;
				startCluster = read_next_cluster_FAT(startCluster);
				if (startCluster == -1) break;
				numberOfClusters++;
			}
            used = used + numberOfClusters;
        }
	}
    
    for (int i = 2; i < size; i++) {
        if (FAT[i] == 0x0000) notUsed++;
		else if (FAT[i] >= 0xFFF8 && FAT[i] <= 0xFFFF) end++;
		else if (FAT[i] == 0xFFFF) dmgd++;
    }

	printf("Klastry zajete : %d\n", used);
	printf("Klastry wolne : %d\n", notUsed);
	printf("Klastry uszkodzone : %d\n", dmgd);
	printf("Klastry konczace lacuchy klastrow : %d\n", end);
	printf("Wielkosc klastra w sektorach : %d\n", boot.BIOS_Parameter_Block.sectors_per_cluster);
	printf("Wielkosc klastra w bajtach : %d\n", boot.BIOS_Parameter_Block.sectors_per_cluster * boot.BIOS_Parameter_Block.bytes_per_sector);
}

int cat(char* name) {
	int id = openFile(name);
	if (id == -1) {
		printf("Plik o poadnej nazwie nie istnieje w aktualnym katalogu\n");
		return id;
	}
	char buff[SECTOR_SIZE * 2] = { 0 };
	int read = 0;

	printf("\n\n");
	while (read != -1) {
		read = readFile(id, buff);
		for (int i = 0; i < read; i++) {
			printf("%c", buff[i]);
		}
	}
	closeFile(id);
	printf("\n\n");
	return 0;
}

int fileInfo(char* name) {

	int id = openFile(name);
	if (id == -1) {
		printf("\nNie poprawna nazwa pliku lub inny blad\n");
	}

	for (list<MFILE>::iterator f = openFiles.begin(); f != openFiles.end(); f++) {
		if (f->ID == id) {

			printf("Pelna nazwa : ");
			printPath(); printf("%s\n", name);

			printf("Atrybuty : ");
			uint8_t b = f->file.attribute_byte;
			if (b & READ_ONLY_FILE) printf("READ_ONLY ");
			if (b & HIDDEN_FILE) printf("HIDDEN_FILE ");
			if (b & SYSTEM_FILE) printf("SYSTEM_FILE ");
			if (b & DIRECTORY) printf("DIRECTORY ");
			if (b & VOLUME_LABEL) printf("VOLUME_LABEL ");
			if (b & ARCHIVED) printf("ARCHIVED ");
			if (b & LFN) printf("LFN ");
			printf("\n");

			printf("Wielkosc : %d bajtow\n", f->file.file_size);
			uint16_t data = f->file.last_write_data;
			uint16_t time = f->file.last_write_time;
			printf("Ostatni zapis : %02d/%02d/%d %02d:%02d\n", month(data), month_day(data), year(data), hours(time), minutes(time));
			data = f->file.last_access_data;
			printf("Ostatni dostep : %02d/%02d/%d\n", month(data), month_day(data), year(data));
			data = f->file.creation_data; time = f->file.creation_time;
			printf("Utworzono : %02d/%02d/%d %02d:%02d\n", month(data), month_day(data), year(data), hours(time), minutes(time));

			int numberOfClusters = 1;
			printf("Lancuch klastrow : [%d]", f->file.starting_cluster);
			int startCluster = f->file.starting_cluster;

			while (1) {
				startCluster = read_next_cluster_FAT(startCluster);
				if (startCluster == -1) break;
				numberOfClusters++;
				printf(", %d", startCluster);
			}
			printf("\nLiczba klastrow : %d\n", numberOfClusters);
			break;
		}
	}

	closeFile(id);
	return 0;
}

int get(char* name) {
	int id = openFile(name);
	if (id == -1) {
		printf("\nNie mozna znalezc pliku o podanej nazwie w obecnym katalogu\n");
		return -1;
	}
	char buff[SECTOR_SIZE * 2] = { 0 };
	int read = 0;

	FILE* f = fopen(name, "w");


	while (read != -1) {
		read = readFile(id, buff);
		for (int i = 0; i < read; i++) fprintf(f, "%c", buff[i]);
	}

	fclose(f);
	closeFile(id);
	printf("\nPobrano plik do katalogu systemowego\n");
	return 0;
}

int zip(char* src1, char* src2, char* dest) {
	if (src1 == NULL || src2 == NULL || dest == NULL) {
		printf("\nPlik o podanej zawie nie instnieje w aktualnym katalogu\n");
		return -1;
	}

	int s1ID = openFile(src1), s2ID = 0;
	if (s1ID == -1) {
		printf("\nPlik o podanej zawie nie instnieje w aktualnym katalogu\n");
		return -1;
	}

	s2ID = openFile(src2);

	if (s2ID == -1) {
		closeFile(s1ID);
		printf("\nPlik o podanej zawie nie instnieje w aktualnym katalogu\n");
		return -1;
	}

	FILE* f = fopen(dest, "w");
	if (f == NULL) {
		closeFile(s1ID);
		closeFile(s2ID);
		printf("\nNie udalo sie utworzyc nowego pliku\n");
		return -1;
	}

	char buff_s1[2 * SECTOR_SIZE] = { 0 };
	char buff_s2[2 * SECTOR_SIZE] = { 0 };
	int size_s1 = readFile(s1ID, buff_s1), cur_s1 = 0;
	int size_s2 = readFile(s2ID, buff_s2), cur_s2 = 0;
	bool f1 = false;
	bool f2 = false;

	while (1) {

		if (cur_s1 < size_s1) {
			while (1) {

				for (; cur_s1 < size_s1; cur_s1++) {

					fprintf(f, "%c", buff_s1[cur_s1]);
					if (buff_s1[cur_s1] == '\n') {

						cur_s1++;
						f1 = true;
						break;
					}
				}

				if (f1 == true) {
					f1 = false;
					break;
				}
				else {
					cur_s1 = 0;
					size_s1 = readFile(s1ID, buff_s1);
					if (size_s1 == -1) {

						fprintf(f, "\n");
						break;
					}
				}
			}
		}

		if (cur_s2 < size_s2) {

			while (1) {

				for (; cur_s2 < size_s2; cur_s2++) {

					fprintf(f, "%c", buff_s2[cur_s2]);
					if (buff_s2[cur_s2] == '\n') {

						cur_s2++;
						f2 = true;
						break;
					}
				}

				if (f2 == true) {
					f2 = false;
					break;
				}
				else {
					cur_s2 = 0;
					size_s2 = readFile(s2ID, buff_s2);
					if (size_s2 == -1) {
						fprintf(f, "\n");
						break;
					}
				}

			}

		}
		if (size_s1 == -1 && size_s2 == -1) break;
	}

	printf("\nUtworzono plik zip z plikow %s i %s\n", src1, src2);
	closeFile(s1ID);
	closeFile(s2ID);
	fclose(f);
	return 0;

}

int main(){

	if ((f = fopen("fat16.bin", "rwb")) == NULL) {
		printf("BŁAD");
		return 0;
	}

	if (fseek(f, 0, SEEK_SET) != 0) {
		printf("BŁAD");
		return 0;
	}

	read_boot_sector();
	calculate_regions();
	FAT = (uint16_t*)malloc((boot.BIOS_Parameter_Block.sectors_per_fat * SECTOR_SIZE));
	if (FAT == NULL) {
		printf("BŁAD");
		return 0;
	}

	readblock(FAT, fat1_start, boot.BIOS_Parameter_Block.sectors_per_fat);
    
	uint8_t sector[SECTOR_SIZE] = { 0 };
	readblock(sector, root_start, 1);
	read_root_directory();

	printf("======================FAT16 VIEWER========================\n\n");


	char line[LINE_LENGTH] = { 0 };
	char* cmd;

	while (1) {

		printf("$> ");

		fgets(line, LINE_LENGTH, stdin);
		for (int i = 0; i < LINE_LENGTH; i++) line[i] = toupper(line[i]);
		if (line[0] == '\n') continue;

		cmd = strtok(line, " \n");


		if (strcmp(cmd, "DIR") == 0) {
			readDirectory(current_directory.id);
		}
		else if (strcmp(cmd, "CD") == 0) {
			cmd = strtok(NULL, " \n");
			if (cmd == NULL) printf("\nNiepoprawny argument\n");
			else cd(cmd);
		}
		else if (strcmp(cmd, "PWD") == 0) {
			printf("Aktualny katlog : ");
			printPath();
			printf("\n");
		}
		else if (strcmp(cmd, "CAT") == 0) {
			cmd = strtok(NULL, " \n");
			if (cmd == NULL) printf("\nNiepoprawny argument\n");
			else cat(cmd);

		}
		else if (strcmp(cmd, "FILEINFO") == 0) {
			cmd = strtok(NULL, " \n");
			if (cmd == NULL) printf("\nNiepoprawny argument\n");
			else fileInfo(cmd);
		}
		else if (strcmp(cmd, "GET") == 0) {
			cmd = strtok(NULL, " \n");
			if (cmd == NULL) printf("\nNiepoprawny argument\n");
			else get(cmd);
		}
		else if (strcmp(cmd, "ZIP") == 0) {
			char* s1 = strtok(NULL, " \n");
			char* s2 = strtok(NULL, " \n");
			cmd = strtok(NULL, " \n");
			if (cmd == NULL || s1 == NULL || s2 == NULL) printf("\nNiepoprawne argumenty\n");
			else zip(s1, s2, cmd);
		}
		else if (strcmp(cmd, "SPACEINFO") == 0) {
			printf("\n");
			spaceinfo();
			printf("\n");
		}
		else if (strcmp(cmd, "ROOTINFO") == 0) {
			printf("\n");
			rootinfo();
			printf("\n");
		}
		else if (strcmp(cmd, "EXIT") == 0) {
			break;
		}else{
            printf("\nNie ma takiego polecenia\n");
        }
	}

	openFiles.clear();
	openDirectories.clear();
	path.clear();
	fclose(f);
	free(FAT);

	return 0;
}