Optimisation tarifs peage (AREA) — PDF -> CSV -> Dijkstra (C++ Win32)

Projet en 2 parties :

Python : extraction automatique d’un PDF de tarifs (AREA) vers un CSV exploitable

C++ / Win32 : calcul du meilleur tarif entre deux gares (Dijkstra), avec options multi-sorties, comparaisons et analyse de gain à chaque sortie.

1) Extraction PDF -> CSV (Python)

Objectif : transformer le PDF tarifaire en un tableau propre pour calculs.

Sortie CSV attendue

Format séparé par ; :

code_entree;gare_entree;code_sortie;gare_sortie;distance_km;tarif_classe_1;tarif_classe_2;tarif_classe_3;tarif_classe_4;tarif_classe_5

Exemple :

15018;ST MARTIN BELLEVUE A41 N;3001;ST QUENTIN FAL. BARRIERE;159.0;26.6;43.7;63.6;81.6;13.6


Le script gère :

normalisation des nombres (virgule/point)

nettoyage des espaces et caractères parasites

structure stable et directement consommable par le code C++

2) Optimisation tarif trajet (C++ / Win32)
Objectif

Trouver le coût minimal entre une gare d’entrée A et une gare de sortie C pour une classe véhicule (1..5), en autorisant :

trajet direct A -> C si disponible dans le CSV

trajet décomposé A -> B -> C (et plus) si la somme des tronçons est moins chère

Algorithme

Représentation en graphe orienté :

noeuds = codes de gare

arêtes = tronçons tarifés (entrée -> sortie)

poids = tarif de la classe choisie

Calcul par Dijkstra étendu sur état (node, nb_segments) afin d’obtenir plusieurs solutions selon le nombre de segments (donc sorties intermédiaires).

segments = transactions péage

sorties intermédiaires = segments - 1

Fonctionnalités clés

GUI Win32 :

dropdown gare entrée / gare sortie (tri alphabétique)

dropdown catégorie véhicule (Classe 1..5)

bouton calcul + bouton export (placeholder si non activé)

Comparaison direct vs meilleur chemin multi-sorties

Affichage détaillé segment par segment :

coût segment, distance segment, cumul coût + cumul distance

Analyse “gain par sortie” :
à chaque étape du meilleur chemin, comparaison locale :

option AVEC sortie : A->B + B->C

option SANS sortie : A->C (direct si existe)

affiche si la sortie est :

NECESSAIRE (pas de tarif direct A->C)

BENEFIQUE (gain > 0)

NEUTRE / NON rentable

Affichage gnu-friendly : remplacement des caractères accentués (é, à, ç, …) en ASCII via ascii_fold_fr() pour logs/exports compatibles.

Filtre Option C (anti-boucles / progress)

Option de sécurité : filtre de progression vers destination.

pré-calcule rem[u] = distance minimale restante de u vers la destination

n’autorise une arête u->v que si rem[v] < rem[u] (avec epsilon)
Cela évite certains chemins absurdes / retours en arrière sur des graphes incomplets.

Interprétation des résultats

Le programme peut afficher un meilleur tarif multi-sorties inférieur au direct si :

certaines gares intermédiaires “cassent” une tranche tarifaire

certains tronçons sont tarifés de manière non linéaire (effets de paliers)

le tarif direct existe mais est plus cher que la somme des sous-tronçons

L’analyse “gain par sortie” est locale (comparaison A->C vs A->B->C), et la somme des gains locaux peut différer de l’écart global si :

plusieurs alternatives existent (meilleure arête parmi doublons)

le chemin global inclut des segments choisis pour minimiser le total, pas chaque gain local indépendamment

certains sauts directs n’existent pas (sortie “nécessaire”)

Structure du dépôt (exemple)

python/

extract_tarifs_area.py : extraction PDF -> CSV

cpp/

peage_optimizer_win32.cpp : GUI + Dijkstra multi-segments + analyse sortie

data/

tarifs_area.csv : CSV généré (ou exemple)

TARIF_AREA.pdf : source (si autorisé à versionner)

Usage rapide

Générer le CSV :

placer TARIF_AREA.pdf

lancer le script Python → produit tarifs_area.csv

Compiler / lancer l’app Win32 :

placer tarifs_area.csv à côté de l’exécutable

choisir entrée / sortie / classe

calculer → obtenir meilleur coût + options par nombre de sorties + détails


///////



extract CSV aera tarif :
python extract_area_classe1.py

compilation dijkstra_peage.cpp :
cl /std:c++17 /O2 dijkstra_peage.cpp user32.lib gdi32.lib