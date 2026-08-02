// ---------------------------------------------------------------------------
// Modele du fichier de secrets - A COPIER, PAS A REMPLIR.
//
//   1. copier ce fichier en  include/secrets.h
//   2. y mettre le SSID et le mot de passe du Wi-Fi
//
// `include/secrets.h` est exclu du depot par .gitignore (plan §16.3) : le mot
// de passe du Wi-Fi ne doit jamais y entrer. Ce modele-ci, lui, est versionne
// justement parce qu'il ne contient rien.
// ---------------------------------------------------------------------------

#pragma once

// --- Reseau ----------------------------------------------------------------

#define WIFI_SSID      "NomDuReseauWiFi"
#define WIFI_PASSWORD  "MotDePasseWiFi"

// --- Identite de ce boitier ------------------------------------------------
//
// CLIM_DEVICE_ID sert de nom d'hote : le boitier repond aussi a
// http://<id>.local/ si le telephone gere le mDNS (iPhone oui, Android
// recent oui). En cas de doute, l'adresse IP est affichee sur la console au
// demarrage, et c'est elle qui marche partout.

#define CLIM_DEVICE_ID    "clim-salon"
#define CLIM_DEVICE_NAME  "Salon"

// --- Second boitier, quand il existera --------------------------------------
//
// Laisser les deux vides tant qu'il n'y en a qu'un : la page n'affichera
// qu'une carte. Sinon, l'adresse complete du second, sans barre finale.

#define CLIM_PEER_NAME  ""
#define CLIM_PEER_BASE  ""   // ex: "http://192.168.1.41"
