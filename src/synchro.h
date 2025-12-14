#ifndef SYNC_H
#define SYNC_H

#include "worker.h"
/**
 * Uruchamia pętlę nieskończoną monitorującą katalog źródłowy
 * przy użyciu inotify. Wszelkie zmiany (nowe pliki, katalogi, edycje)
 * są natychmiast odwzorowywane w katalogu docelowym.
 *
 * @param source_dir Ścieżka do katalogu źródłowego (musi istnieć)
 * @param target_dir Ścieżka do katalogu docelowego
 */
void synchronize(const char *source_dir, const char *target_dir);
int prep_dirs(char* src, char* dst, workerList* workers);
#endif // SYNC_H