PS4RT - Star Ocean 5 - Windows x64 - Version 0.3.5
==================================================

Aucun fichier du jeu n'est inclus. Utilisez votre propre dossier CUSA03219 extrait et en clair,
contenant eboot.bin, sce_module et sce_sys.

Prerequis joueur
- Windows 10/11 x64 et un processeur x86-64 compatible avec les instructions verifiees au lancement.
- Pilote graphique recent avec Vulkan 1.3.
- Aucun Python, MSYS2, PowerShell ou autre runtime externe n'est requis.
- Les DLL C/C++ necessaires sont incluses a cote des deux executables.

Lancement recommande
  Double-cliquez sur PS4RT-Launcher.exe.

Le launcher Cockpit permet de choisir et memoriser CUSA03219, preparer et jouer sans fenetre
PowerShell, regler l'affichage, FSR, la resolution de rendu, les IPS, VSync, le filtrage anisotrope
et les shaders asynchrones. Il affiche la derniere sauvegarde, peut creer un backup avant chaque
lancement, utiliser un dossier de sauvegarde personnalise et exporter les diagnostics.

Le bouton de selection PKG utilise PS4RT-PkgExtractor.exe et l'extracteur ShadPS4Plus integre.
Le contenu est place par defaut dans
  %LOCALAPPDATA%\PS4RT\Games\Imports\<nom-du-pkg>-<date>
Le launcher demande confirmation, permet de choisir et memorise le dossier d'extraction, affiche
une progression chronometree et permet d'annuler en nettoyant les fichiers partiels. Le dossier CUSA03219 extrait est automatiquement detecte et
memorise. L'extraction s'effectue sans console visible et ses messages en anglais sont disponibles
dans Journal et diagnostic.
Utilisez uniquement des paquets dont vous possedez legalement une copie.

Par defaut, les fichiers prepares, reglages, sauvegardes et caches sont places sous
out\CUSA03219\. Le dossier des sauvegardes peut etre personnalise dans l'onglet Jeu.
Le preparateur C++ integre cree l'image de travail sans modifier le dossier original du jeu.
PS4RT-Launcher.exe, ps4rt.exe et PS4RT-PkgExtractor.exe doivent rester ensemble dans le meme dossier.
Le sous-dossier tools\pkg-extractor doit lui aussi rester dans le paquet.
Le programme est experimental et n'a pas ete valide sur l'ensemble du jeu ni sur toutes les cartes.
