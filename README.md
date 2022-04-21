# Istruzioni per compilare ed eseguire

Per eseguire il programma da terminale:
1. Installare la [libreria fornita da upmem](https://sdk.upmem.com/).
2. Scaricare ed aprire da terminale la repo sovrastante.
3. Eseguire con ```source``` lo script ```upmem_env.sh``` contenuto nella libreria upmem specificando il parametro ```simulator```. Esempio:
   ```
   source ~/Scrivania/upmem-sdk/upmem_env.sh simulator
   ```
4. Modificare la varibile ```rootdir``` all'interno di ```run.py``` con il path della repo nel proprio dispositivo.
5. Eseguire il programma tramite lo script ```run.py``` specificando l'applicazione da eseguire:
   ```
   python3 run.py KCC
   ```


# Note sul codice

La struttura generale del codice segue le linee guide indicate nei [benchmark](https://github.com/CMU-SAFARI/prim-benchmarks) prodotti da SAFARI Research Group.<br>
La cartella host contiene il codice eseguito nella CPU centrale mentre la cartella dpu contiene il codice che verrebbe eseguito nelle memorie.

Il programma esegue l'applicazione specificata (al momento ho implementato solo k-center con coreset) su un dataset di punti. Il valore dei vari punti, la loro dimensione ed il numero di centri da estrarre sono generati automaticamente.<br>
I centri vengono calcolati sia dalle DPU che dall'host application così da poter verificare la correttezza dei valori ottenuti. 

Per ogni applicazione vengono effettuati vari test istanziando un diverso numero di DPU e di tasklets.<br>
I risultati di ogni test sono riportati nella cartella ```/profile``` creata quando si esegue per la prima volta un'applicazione.


# TODO
I problemi noti/elementi da rivedere sono segnalati nel codice con un conmmento ```//TODO: ...```<br>
I principali problemi noti al momento sono:
+ Gestione di un'undefined behavior nel file ```app.c``` (commento riga 154).
+ Mancato utilizzo di dataset esterni per verificare l'effettiva correttezza dell'algoritmo.
+ Implementazione della funzione che calcola la distanza tra i vari punti: dataset contenenti punti con valori e/o dimensione elevati generano overflow usando la distanza di Minkowski. Al momento questo problema viene evitato limitando la dimensione massima ad 5 e il valore massimo di ogni punto ad 1000.
+ Largo utilizzo di moltiplicazioni all'interno del codice che viene eseguito nelle DPU (sono operazioni molto costose). Questo è in parte risolvibile tramite l'utilizzo di operazioni di bitshift ove possibile.
+ Implementazione di timer per verificare i tempi di esecuzione dell'argoritmo su CPU e su DPU: il simulatore permette infatti di contare accuratemente solo il numero di istruzioni effettuate.
+ Sistema di verifica dei risultati genera "falsi negativi": nel caso in cui più centri candidati avessero la stessa distanza minima, l'host e le DPU potrebbero restituire risultati diversi (anche se matematicamente analoghi). Questo è particolarmente evidente quando la dimensione è pari ad 1 ed il numero di collisione è elevato.