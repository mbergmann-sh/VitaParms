// SPDX-License-Identifier: MIT
#include "drugclass.h"
#include "medschedule.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>

namespace {

QString tr(const char *s) { return QCoreApplication::translate("DrugClass", s); }

enum Mask { B = 1, P = 2, A = 4, H = 8, C = 16 };   // H = Blutdrucksenker, C = Kortison
enum Rule { None = 0, PerUnitMg, Tilidin, Ass, AlwaysUncertain,
            OtherUse,    // passt zur Gruppe, wird aber oft auch aus anderen Gründen verordnet ("ggf.")
            Cortico };   // Glukokortikoid; subclass = Standard-Anwendungsweg, Darreichungsform hat Vorrang

struct Entry {
    const char *key;        // Kleinbuchstaben
    bool brand;             // Handelsname: ganzes Wort; Wirkstoff: Wortanfang
    const char *substance;  // Anzeige
    int mask;
    const char *subclass;
    Rule rule = None;
    double limitMg = 0;     // PerUnitMg: bis zu dieser Menge je Einheit ausgenommen (kein BtM)
};

// Anzeigenamen der Wirkstoffe (für lupdate; englisch: INN-Schreibweise)
[[maybe_unused]] static const char *const kSubstanceNames[] = {
    QT_TRANSLATE_NOOP("DrugSubstance", "ASS/Dipyridamol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "ASS/Paracetamol/Coffein"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Aceclofenac"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Acenocoumarol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Acetylsalicylsäure"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Almotriptan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Amfetamin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Apixaban"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Bromazepam"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Buprenorphin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Buprenorphin/Naloxon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Celecoxib"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Certoparin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Cilostazol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Clopidogrel"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Cocain"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Codein"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Dabigatran"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Dalteparin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Dexamfetamin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Dexketoprofen"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Diamorphin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Diazepam"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Diclofenac"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Dihydrocodein"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Dipyridamol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Edoxaban"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Eletriptan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Enoxaparin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Ergotamin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Etoricoxib"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Fentanyl"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Flunitrazepam"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Flupirtin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Fondaparinux"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Frovatriptan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Heparin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Hydrocodon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Hydromorphon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Ibuprofen"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Indometacin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Ketoprofen"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Levomethadon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Lisdexamfetamin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Lorazepam"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Lornoxicam"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Meloxicam"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Metamizol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Methadon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Methylphenidat"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Morphin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Nabilon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Nadroparin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Nalbuphin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Naproxen"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Naratriptan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Natriumoxybat"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Oxazepam"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Oxycodon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Oxycodon/Naloxon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Paracetamol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Parecoxib"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Pentazocin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Pentobarbital"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Pethidin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Phenobarbital"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Phenprocoumon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Piritramid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Piroxicam"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Prasugrel"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Rivaroxaban"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Rizatriptan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Sufentanil"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Sumatriptan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Tapentadol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Ticagrelor"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Ticlopidin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Tilidin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Tilidin/Naloxon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Tinzaparin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Tramadol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Warfarin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Zolmitriptan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Zolpidem"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Aliskiren"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Amilorid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Amlodipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Amlodipin/Valsartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Atenolol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Azilsartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Beclometason"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Benazepril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Betamethason"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Betaxolol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Bisoprolol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Budesonid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Bumetanid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Bunazosin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Candesartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Captopril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Carvedilol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Celiprolol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Chlortalidon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Ciclesonid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Cilazapril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Clobetasol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Clobetason"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Clonidin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Cortison"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Deflazacort"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Dexamethason"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Dihydralazin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Diltiazem"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Doxazosin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Enalapril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Eplerenon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Eprosartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Felodipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Fludrocortison"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Fluocinolon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Fluocortolon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Fluticason"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Fosinopril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Furosemid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Hydralazin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Hydrochlorothiazid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Hydrocortison"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Imidapril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Indapamid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Irbesartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Isradipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Labetalol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Lacidipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Lercanidipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Lisinopril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Losartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Manidipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Methyldopa"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Methylprednisolon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Metoprolol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Minoxidil"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Moexipril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Mometason"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Moxonidin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Nebivolol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Nicardipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Nifedipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Nisoldipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Nitrendipin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Olmesartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Perindopril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Pindolol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Piretanid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Prazosin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Prednicarbat"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Prednisolon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Prednison"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Propranolol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Quinapril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Ramipril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Rilmenidin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Sacubitril/Valsartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Sotalol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Spirapril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Spironolacton"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Talinolol"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Telmisartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Terazosin"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Torasemid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Trandolapril"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Triamcinolon"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Triamteren"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Urapidil"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Valsartan"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Verapamil"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Xipamid"),
    QT_TRANSLATE_NOOP("DrugSubstance", "Zofenopril")
};
static QString substanceName(const char *s) { return QCoreApplication::translate("DrugSubstance", s); }

// Hinweis: Reihenfolge = Priorität (längere/spezifischere Schlüssel zuerst)
const Entry kEntries[] = {
    // --- Opioide (BtM, Anlage III BtMG) – zugleich Schmerzmittel
    { "hydromorphon", false, "Hydromorphon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "morphin", false, "Morphin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "oxycodon", false, "Oxycodon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "fentanyl", false, "Fentanyl", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "buprenorphin", false, "Buprenorphin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "tapentadol", false, "Tapentadol", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "levomethadon", false, "Levomethadon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "methadon", false, "Methadon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "pethidin", false, "Pethidin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "piritramid", false, "Piritramid", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "hydrocodon", false, "Hydrocodon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "sufentanil", false, "Sufentanil", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "diamorphin", false, "Diamorphin", B, QT_TRANSLATE_NOOP("DrugClass", "Opioid (Substitution)") },
    { "mst", true, "Morphin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "sevredol", true, "Morphin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "capros", true, "Morphin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "palladon", true, "Hydromorphon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "jurnista", true, "Hydromorphon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "oxygesic", true, "Oxycodon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "targin", true, "Oxycodon/Naloxon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "durogesic", true, "Fentanyl", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "abstral", true, "Fentanyl", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "effentora", true, "Fentanyl", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "instanyl", true, "Fentanyl", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "pecfent", true, "Fentanyl", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "transtec", true, "Buprenorphin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "norspan", true, "Buprenorphin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "temgesic", true, "Buprenorphin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "subutex", true, "Buprenorphin", B, QT_TRANSLATE_NOOP("DrugClass", "Opioid (Substitution)") },
    { "suboxone", true, "Buprenorphin/Naloxon", B, QT_TRANSLATE_NOOP("DrugClass", "Opioid (Substitution)") },
    { "buvidal", true, "Buprenorphin", B, QT_TRANSLATE_NOOP("DrugClass", "Opioid (Substitution)") },
    { "palexia", true, "Tapentadol", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "l-polamidon", true, "Levomethadon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "polamidon", true, "Levomethadon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "dipidolor", true, "Piritramid", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    { "dolantin", true, "Pethidin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    // --- schwache Opioide mit Ausnahmeregeln
    { "dihydrocodein", false, "Dihydrocodein", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid (schwach)"), PerUnitMg, 100 },
    { "dhc", true, "Dihydrocodein", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid (schwach)"), PerUnitMg, 100 },
    { "paracodin", true, "Dihydrocodein", B, QT_TRANSLATE_NOOP("DrugClass", "Opioid (Antitussivum)"), PerUnitMg, 100 },
    { "codein", false, "Codein", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid (schwach)"), PerUnitMg, 100 },
    { "tilidin", false, "Tilidin", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid (schwach)"), Tilidin },
    { "valoron", true, "Tilidin/Naloxon", B | P, QT_TRANSLATE_NOOP("DrugClass", "Opioid (schwach)"), Tilidin },
    { "tramadol", false, "Tramadol", P, QT_TRANSLATE_NOOP("DrugClass", "Opioid (schwach, kein BtM)") },
    { "tramal", true, "Tramadol", P, QT_TRANSLATE_NOOP("DrugClass", "Opioid (schwach, kein BtM)") },
    { "tramadolor", true, "Tramadol", P, QT_TRANSLATE_NOOP("DrugClass", "Opioid (schwach, kein BtM)") },
    { "nalbuphin", false, "Nalbuphin", P, QT_TRANSLATE_NOOP("DrugClass", "Opioid (kein BtM)") },
    { "pentazocin", false, "Pentazocin", P, QT_TRANSLATE_NOOP("DrugClass", "Opioid") },
    // --- weitere BtM (keine Schmerzmittel)
    { "lisdexamfetamin", false, "Lisdexamfetamin", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "dexamfetamin", false, "Dexamfetamin", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "amfetamin", false, "Amfetamin", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans") },
    { "methylphenidat", false, "Methylphenidat", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "ritalin", true, "Methylphenidat", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "medikinet", true, "Methylphenidat", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "concerta", true, "Methylphenidat", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "equasym", true, "Methylphenidat", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "kinecteen", true, "Methylphenidat", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "elvanse", true, "Lisdexamfetamin", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "attentin", true, "Dexamfetamin", B, QT_TRANSLATE_NOOP("DrugClass", "Stimulans (ADHS)") },
    { "nabilon", false, "Nabilon", B, QT_TRANSLATE_NOOP("DrugClass", "Cannabinoid") },
    { "canemes", true, "Nabilon", B, QT_TRANSLATE_NOOP("DrugClass", "Cannabinoid") },
    { "flunitrazepam", false, "Flunitrazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin") },
    { "rohypnol", true, "Flunitrazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin") },
    { "pentobarbital", false, "Pentobarbital", B, QT_TRANSLATE_NOOP("DrugClass", "Barbiturat") },
    { "cocain", false, "Cocain", B, QT_TRANSLATE_NOOP("DrugClass", "Lokalanästhetikum") },
    { "kokain", false, "Cocain", B, QT_TRANSLATE_NOOP("DrugClass", "Lokalanästhetikum") },
    { "natriumoxybat", false, "Natriumoxybat", B, QT_TRANSLATE_NOOP("DrugClass", "Narkolepsie"), AlwaysUncertain },
    { "xyrem", true, "Natriumoxybat", B, QT_TRANSLATE_NOOP("DrugClass", "Narkolepsie"), AlwaysUncertain },
    // Benzodiazepine & Co.: BtM nur oberhalb der Grenze je Einheit (darunter ausgenommen)
    { "diazepam", false, "Diazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin"), PerUnitMg, 10 },
    { "valium", true, "Diazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin"), PerUnitMg, 10 },
    { "lorazepam", false, "Lorazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin"), PerUnitMg, 2.5 },
    { "tavor", true, "Lorazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin"), PerUnitMg, 2.5 },
    { "oxazepam", false, "Oxazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin"), PerUnitMg, 50 },
    { "adumbran", true, "Oxazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin"), PerUnitMg, 50 },
    { "bromazepam", false, "Bromazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin"), PerUnitMg, 6 },
    { "lexotanil", true, "Bromazepam", B, QT_TRANSLATE_NOOP("DrugClass", "Benzodiazepin"), PerUnitMg, 6 },
    { "zolpidem", false, "Zolpidem", B, QT_TRANSLATE_NOOP("DrugClass", "Z-Substanz"), PerUnitMg, 8.5 },
    { "stilnox", true, "Zolpidem", B, QT_TRANSLATE_NOOP("DrugClass", "Z-Substanz"), PerUnitMg, 8.5 },
    { "phenobarbital", false, "Phenobarbital", B, QT_TRANSLATE_NOOP("DrugClass", "Barbiturat"), PerUnitMg, 300 },
    { "luminal", true, "Phenobarbital", B, QT_TRANSLATE_NOOP("DrugClass", "Barbiturat"), PerUnitMg, 300 },
    // --- Nicht-Opioid-Schmerzmittel
    { "ibuprofen", false, "Ibuprofen", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "nurofen", true, "Ibuprofen", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "ibu", true, "Ibuprofen", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "diclofenac", false, "Diclofenac", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "voltaren", true, "Diclofenac", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "dexketoprofen", false, "Dexketoprofen", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "ketoprofen", false, "Ketoprofen", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "naproxen", false, "Naproxen", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "indometacin", false, "Indometacin", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "meloxicam", false, "Meloxicam", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "piroxicam", false, "Piroxicam", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "lornoxicam", false, "Lornoxicam", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "aceclofenac", false, "Aceclofenac", P, QT_TRANSLATE_NOOP("DrugClass", "NSAR") },
    { "etoricoxib", false, "Etoricoxib", P, QT_TRANSLATE_NOOP("DrugClass", "Coxib") },
    { "arcoxia", true, "Etoricoxib", P, QT_TRANSLATE_NOOP("DrugClass", "Coxib") },
    { "celecoxib", false, "Celecoxib", P, QT_TRANSLATE_NOOP("DrugClass", "Coxib") },
    { "celebrex", true, "Celecoxib", P, QT_TRANSLATE_NOOP("DrugClass", "Coxib") },
    { "parecoxib", false, "Parecoxib", P, QT_TRANSLATE_NOOP("DrugClass", "Coxib") },
    { "metamizol", false, "Metamizol", P, QT_TRANSLATE_NOOP("DrugClass", "Nicht-Opioid-Analgetikum") },
    { "novaminsulfon", false, "Metamizol", P, QT_TRANSLATE_NOOP("DrugClass", "Nicht-Opioid-Analgetikum") },
    { "novalgin", true, "Metamizol", P, QT_TRANSLATE_NOOP("DrugClass", "Nicht-Opioid-Analgetikum") },
    { "berlosin", true, "Metamizol", P, QT_TRANSLATE_NOOP("DrugClass", "Nicht-Opioid-Analgetikum") },
    { "paracetamol", false, "Paracetamol", P, QT_TRANSLATE_NOOP("DrugClass", "Nicht-Opioid-Analgetikum") },
    { "acetaminophen", false, "Paracetamol", P, QT_TRANSLATE_NOOP("DrugClass", "Nicht-Opioid-Analgetikum") },
    { "ben-u-ron", true, "Paracetamol", P, QT_TRANSLATE_NOOP("DrugClass", "Nicht-Opioid-Analgetikum") },
    { "perfalgan", true, "Paracetamol", P, QT_TRANSLATE_NOOP("DrugClass", "Nicht-Opioid-Analgetikum") },
    { "flupirtin", false, "Flupirtin", P, QT_TRANSLATE_NOOP("DrugClass", "Nicht-Opioid-Analgetikum") },
    { "sumatriptan", false, "Sumatriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "imigran", true, "Sumatriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "rizatriptan", false, "Rizatriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "maxalt", true, "Rizatriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "zolmitriptan", false, "Zolmitriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "ascotop", true, "Zolmitriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "naratriptan", false, "Naratriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "eletriptan", false, "Eletriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "relpax", true, "Eletriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "almotriptan", false, "Almotriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "frovatriptan", false, "Frovatriptan", P, QT_TRANSLATE_NOOP("DrugClass", "Triptan (Migräne)") },
    { "ergotamin", false, "Ergotamin", P, QT_TRANSLATE_NOOP("DrugClass", "Migränemittel") },
    { "thomapyrin", true, "ASS/Paracetamol/Coffein", P, QT_TRANSLATE_NOOP("DrugClass", "Kombinationsanalgetikum") },
    // --- ASS: niedrig dosiert Thrombozytenaggregationshemmer, hoch dosiert Schmerzmittel
    { "acetylsalicylsäure", false, "Acetylsalicylsäure", A | P, QT_TRANSLATE_NOOP("DrugClass", ""), Ass },
    { "acetylsalicylsaeure", false, "Acetylsalicylsäure", A | P, QT_TRANSLATE_NOOP("DrugClass", ""), Ass },
    { "acetylsalicylic", false, "Acetylsalicylsäure", A | P, QT_TRANSLATE_NOOP("DrugClass", ""), Ass },
    { "herzass", true, "Acetylsalicylsäure", A | P, QT_TRANSLATE_NOOP("DrugClass", ""), Ass },
    { "godamed", true, "Acetylsalicylsäure", A | P, QT_TRANSLATE_NOOP("DrugClass", ""), Ass },
    { "aspirin", true, "Acetylsalicylsäure", A | P, QT_TRANSLATE_NOOP("DrugClass", ""), Ass },
    { "ass", true, "Acetylsalicylsäure", A | P, QT_TRANSLATE_NOOP("DrugClass", ""), Ass },
    // --- Blutverdünner / Antikoagulanzien
    { "phenprocoumon", false, "Phenprocoumon", A, QT_TRANSLATE_NOOP("DrugClass", "Vitamin-K-Antagonist") },
    { "marcumar", true, "Phenprocoumon", A, QT_TRANSLATE_NOOP("DrugClass", "Vitamin-K-Antagonist") },
    { "falithrom", true, "Phenprocoumon", A, QT_TRANSLATE_NOOP("DrugClass", "Vitamin-K-Antagonist") },
    { "warfarin", false, "Warfarin", A, QT_TRANSLATE_NOOP("DrugClass", "Vitamin-K-Antagonist") },
    { "coumadin", true, "Warfarin", A, QT_TRANSLATE_NOOP("DrugClass", "Vitamin-K-Antagonist") },
    { "acenocoumarol", false, "Acenocoumarol", A, QT_TRANSLATE_NOOP("DrugClass", "Vitamin-K-Antagonist") },
    { "apixaban", false, "Apixaban", A, QT_TRANSLATE_NOOP("DrugClass", "DOAK (Faktor-Xa-Hemmer)") },
    { "eliquis", true, "Apixaban", A, QT_TRANSLATE_NOOP("DrugClass", "DOAK (Faktor-Xa-Hemmer)") },
    { "rivaroxaban", false, "Rivaroxaban", A, QT_TRANSLATE_NOOP("DrugClass", "DOAK (Faktor-Xa-Hemmer)") },
    { "xarelto", true, "Rivaroxaban", A, QT_TRANSLATE_NOOP("DrugClass", "DOAK (Faktor-Xa-Hemmer)") },
    { "edoxaban", false, "Edoxaban", A, QT_TRANSLATE_NOOP("DrugClass", "DOAK (Faktor-Xa-Hemmer)") },
    { "lixiana", true, "Edoxaban", A, QT_TRANSLATE_NOOP("DrugClass", "DOAK (Faktor-Xa-Hemmer)") },
    { "dabigatran", false, "Dabigatran", A, QT_TRANSLATE_NOOP("DrugClass", "DOAK (Thrombinhemmer)") },
    { "pradaxa", true, "Dabigatran", A, QT_TRANSLATE_NOOP("DrugClass", "DOAK (Thrombinhemmer)") },
    { "enoxaparin", false, "Enoxaparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "clexane", true, "Enoxaparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "inhixa", true, "Enoxaparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "dalteparin", false, "Dalteparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "fragmin", true, "Dalteparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "tinzaparin", false, "Tinzaparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "innohep", true, "Tinzaparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "certoparin", false, "Certoparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "mono-embolex", true, "Certoparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "nadroparin", false, "Nadroparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "fraxiparin", true, "Nadroparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin (NMH)") },
    { "heparin", false, "Heparin", A, QT_TRANSLATE_NOOP("DrugClass", "Heparin") },
    { "fondaparinux", false, "Fondaparinux", A, QT_TRANSLATE_NOOP("DrugClass", "Faktor-Xa-Hemmer (parenteral)") },
    { "arixtra", true, "Fondaparinux", A, QT_TRANSLATE_NOOP("DrugClass", "Faktor-Xa-Hemmer (parenteral)") },
    { "clopidogrel", false, "Clopidogrel", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "plavix", true, "Clopidogrel", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "iscover", true, "Clopidogrel", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "prasugrel", false, "Prasugrel", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "efient", true, "Prasugrel", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "ticagrelor", false, "Ticagrelor", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "brilique", true, "Ticagrelor", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "ticlopidin", false, "Ticlopidin", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "dipyridamol", false, "Dipyridamol", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "aggrenox", true, "ASS/Dipyridamol", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "cilostazol", false, "Cilostazol", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    { "pletal", true, "Cilostazol", A, QT_TRANSLATE_NOOP("DrugClass", "Thrombozytenaggregationshemmer") },
    // --- Blutdrucksenker (Antihypertensiva)
    { "ramipril", false, "Ramipril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "enalapril", false, "Enalapril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "lisinopril", false, "Lisinopril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "captopril", false, "Captopril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "perindopril", false, "Perindopril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "fosinopril", false, "Fosinopril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "quinapril", false, "Quinapril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "benazepril", false, "Benazepril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "trandolapril", false, "Trandolapril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "moexipril", false, "Moexipril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "zofenopril", false, "Zofenopril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "cilazapril", false, "Cilazapril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "spirapril", false, "Spirapril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "imidapril", false, "Imidapril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "sacubitril", false, "Sacubitril/Valsartan", H, QT_TRANSLATE_NOOP("DrugClass", "ARNI (Sacubitril/Valsartan)"), OtherUse },
    { "candesartan", false, "Candesartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "valsartan", false, "Valsartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "losartan", false, "Losartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "irbesartan", false, "Irbesartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "olmesartan", false, "Olmesartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "telmisartan", false, "Telmisartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "eprosartan", false, "Eprosartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "azilsartan", false, "Azilsartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "amlodipin", false, "Amlodipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "felodipin", false, "Felodipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "lercanidipin", false, "Lercanidipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "nitrendipin", false, "Nitrendipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "nifedipin", false, "Nifedipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "lacidipin", false, "Lacidipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "nisoldipin", false, "Nisoldipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "manidipin", false, "Manidipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "isradipin", false, "Isradipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "nicardipin", false, "Nicardipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "verapamil", false, "Verapamil", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "diltiazem", false, "Diltiazem", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "metoprolol", false, "Metoprolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "bisoprolol", false, "Bisoprolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "nebivolol", false, "Nebivolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "carvedilol", false, "Carvedilol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "atenolol", false, "Atenolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "celiprolol", false, "Celiprolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "betaxolol", false, "Betaxolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "talinolol", false, "Talinolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "pindolol", false, "Pindolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "labetalol", false, "Labetalol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "propranolol", false, "Propranolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker"), OtherUse },
    { "sotalol", false, "Sotalol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker"), OtherUse },
    { "hydrochlorothiazid", false, "Hydrochlorothiazid", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "chlortalidon", false, "Chlortalidon", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "chlorthalidon", false, "Chlortalidon", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "indapamid", false, "Indapamid", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "xipamid", false, "Xipamid", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "hct", true, "Hydrochlorothiazid", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "furosemid", false, "Furosemid", H, QT_TRANSLATE_NOOP("DrugClass", "Schleifendiuretikum"), OtherUse },
    { "torasemid", false, "Torasemid", H, QT_TRANSLATE_NOOP("DrugClass", "Schleifendiuretikum"), OtherUse },
    { "piretanid", false, "Piretanid", H, QT_TRANSLATE_NOOP("DrugClass", "Schleifendiuretikum"), OtherUse },
    { "bumetanid", false, "Bumetanid", H, QT_TRANSLATE_NOOP("DrugClass", "Schleifendiuretikum"), OtherUse },
    { "spironolacton", false, "Spironolacton", H, QT_TRANSLATE_NOOP("DrugClass", "Kaliumsparendes Diuretikum/Aldosteronantagonist"), OtherUse },
    { "eplerenon", false, "Eplerenon", H, QT_TRANSLATE_NOOP("DrugClass", "Kaliumsparendes Diuretikum/Aldosteronantagonist"), OtherUse },
    { "amilorid", false, "Amilorid", H, QT_TRANSLATE_NOOP("DrugClass", "Kaliumsparendes Diuretikum/Aldosteronantagonist"), OtherUse },
    { "triamteren", false, "Triamteren", H, QT_TRANSLATE_NOOP("DrugClass", "Kaliumsparendes Diuretikum/Aldosteronantagonist"), OtherUse },
    { "prazosin", false, "Prazosin", H, QT_TRANSLATE_NOOP("DrugClass", "Alpha-Blocker") },
    { "urapidil", false, "Urapidil", H, QT_TRANSLATE_NOOP("DrugClass", "Alpha-Blocker") },
    { "bunazosin", false, "Bunazosin", H, QT_TRANSLATE_NOOP("DrugClass", "Alpha-Blocker") },
    { "doxazosin", false, "Doxazosin", H, QT_TRANSLATE_NOOP("DrugClass", "Alpha-Blocker"), OtherUse },
    { "terazosin", false, "Terazosin", H, QT_TRANSLATE_NOOP("DrugClass", "Alpha-Blocker"), OtherUse },
    { "moxonidin", false, "Moxonidin", H, QT_TRANSLATE_NOOP("DrugClass", "Zentral wirksames Antihypertensivum") },
    { "methyldopa", false, "Methyldopa", H, QT_TRANSLATE_NOOP("DrugClass", "Zentral wirksames Antihypertensivum") },
    { "rilmenidin", false, "Rilmenidin", H, QT_TRANSLATE_NOOP("DrugClass", "Zentral wirksames Antihypertensivum") },
    { "clonidin", false, "Clonidin", H, QT_TRANSLATE_NOOP("DrugClass", "Zentral wirksames Antihypertensivum"), OtherUse },
    { "aliskiren", false, "Aliskiren", H, QT_TRANSLATE_NOOP("DrugClass", "Renininhibitor") },
    { "dihydralazin", false, "Dihydralazin", H, QT_TRANSLATE_NOOP("DrugClass", "Vasodilatator") },
    { "hydralazin", false, "Hydralazin", H, QT_TRANSLATE_NOOP("DrugClass", "Vasodilatator") },
    { "minoxidil", false, "Minoxidil", H, QT_TRANSLATE_NOOP("DrugClass", "Vasodilatator"), OtherUse },
    { "delix", true, "Ramipril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "vesdil", true, "Ramipril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "xanef", true, "Enalapril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "acerbon", true, "Lisinopril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "coversum", true, "Perindopril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "lopirin", true, "Captopril", H, QT_TRANSLATE_NOOP("DrugClass", "ACE-Hemmer") },
    { "entresto", true, "Sacubitril/Valsartan", H, QT_TRANSLATE_NOOP("DrugClass", "ARNI (Sacubitril/Valsartan)"), OtherUse },
    { "blopress", true, "Candesartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "atacand", true, "Candesartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "diovan", true, "Valsartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "provas", true, "Valsartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "exforge", true, "Amlodipin/Valsartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "lorzaar", true, "Losartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "cozaar", true, "Losartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "aprovel", true, "Irbesartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "karvea", true, "Irbesartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "olmetec", true, "Olmesartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "votum", true, "Olmesartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "micardis", true, "Telmisartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "kinzalmono", true, "Telmisartan", H, QT_TRANSLATE_NOOP("DrugClass", "Sartan (AT1-Blocker)") },
    { "norvasc", true, "Amlodipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "bayotensin", true, "Nitrendipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "adalat", true, "Nifedipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "carmen", true, "Lercanidipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "corifeo", true, "Lercanidipin", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "isoptin", true, "Verapamil", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "falicard", true, "Verapamil", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "dilzem", true, "Diltiazem", H, QT_TRANSLATE_NOOP("DrugClass", "Calciumantagonist") },
    { "beloc", true, "Metoprolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "lopresor", true, "Metoprolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "concor", true, "Bisoprolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "nebilet", true, "Nebivolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "dilatrend", true, "Carvedilol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "querto", true, "Carvedilol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "tenormin", true, "Atenolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker") },
    { "dociton", true, "Propranolol", H, QT_TRANSLATE_NOOP("DrugClass", "Betablocker"), OtherUse },
    { "esidrix", true, "Hydrochlorothiazid", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "hygroton", true, "Chlortalidon", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "natrilix", true, "Indapamid", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "aquaphor", true, "Xipamid", H, QT_TRANSLATE_NOOP("DrugClass", "Thiazid-Diuretikum") },
    { "lasix", true, "Furosemid", H, QT_TRANSLATE_NOOP("DrugClass", "Schleifendiuretikum"), OtherUse },
    { "torem", true, "Torasemid", H, QT_TRANSLATE_NOOP("DrugClass", "Schleifendiuretikum"), OtherUse },
    { "unat", true, "Torasemid", H, QT_TRANSLATE_NOOP("DrugClass", "Schleifendiuretikum"), OtherUse },
    { "aldactone", true, "Spironolacton", H, QT_TRANSLATE_NOOP("DrugClass", "Kaliumsparendes Diuretikum/Aldosteronantagonist"), OtherUse },
    { "inspra", true, "Eplerenon", H, QT_TRANSLATE_NOOP("DrugClass", "Kaliumsparendes Diuretikum/Aldosteronantagonist"), OtherUse },
    { "cardular", true, "Doxazosin", H, QT_TRANSLATE_NOOP("DrugClass", "Alpha-Blocker"), OtherUse },
    { "diblocin", true, "Doxazosin", H, QT_TRANSLATE_NOOP("DrugClass", "Alpha-Blocker"), OtherUse },
    { "ebrantil", true, "Urapidil", H, QT_TRANSLATE_NOOP("DrugClass", "Alpha-Blocker") },
    { "cynt", true, "Moxonidin", H, QT_TRANSLATE_NOOP("DrugClass", "Zentral wirksames Antihypertensivum") },
    { "physiotens", true, "Moxonidin", H, QT_TRANSLATE_NOOP("DrugClass", "Zentral wirksames Antihypertensivum") },
    { "catapresan", true, "Clonidin", H, QT_TRANSLATE_NOOP("DrugClass", "Zentral wirksames Antihypertensivum"), OtherUse },
    { "presinol", true, "Methyldopa", H, QT_TRANSLATE_NOOP("DrugClass", "Zentral wirksames Antihypertensivum") },
    { "rasilez", true, "Aliskiren", H, QT_TRANSLATE_NOOP("DrugClass", "Renininhibitor") },
    { "nepresol", true, "Dihydralazin", H, QT_TRANSLATE_NOOP("DrugClass", "Vasodilatator") },
    { "lonolox", true, "Minoxidil", H, QT_TRANSLATE_NOOP("DrugClass", "Vasodilatator") },
    // --- Glukokortikoide ("Kortison"); subclass = üblicher Anwendungsweg
    { "methylprednisolon", false, "Methylprednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "prednisolon", false, "Prednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "prednison", false, "Prednison", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "dexamethason", false, "Dexamethason", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "hydrocortison", false, "Hydrocortison", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "fludrocortison", false, "Fludrocortison", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "cortison", false, "Cortison", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "kortison", false, "Cortison", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "triamcinolon", false, "Triamcinolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "betamethason", false, "Betamethason", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "deflazacort", false, "Deflazacort", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "budesonid", false, "Budesonid", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "beclometason", false, "Beclometason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "beclomethason", false, "Beclometason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "fluticason", false, "Fluticason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "mometason", false, "Mometason", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "ciclesonid", false, "Ciclesonid", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "clobetasol", false, "Clobetasol", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "prednicarbat", false, "Prednicarbat", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "fluocinolon", false, "Fluocinolon", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "clobetason", false, "Clobetason", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "decortin h", true, "Prednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "solu-decortin", true, "Prednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "decortin", true, "Prednison", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "prednihexal", true, "Prednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "prednisolut", true, "Prednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "urbason", true, "Methylprednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "medrate", true, "Methylprednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "metypred", true, "Methylprednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "fortecortin", true, "Dexamethason", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "celestan", true, "Betamethason", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "celestamine", true, "Betamethason", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "betnesol", true, "Betamethason", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "volon", true, "Triamcinolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "lederlon", true, "Triamcinolon", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "astonin", true, "Fludrocortison", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "pulmicort", true, "Budesonid", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "budecort", true, "Budesonid", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "novopulmon", true, "Budesonid", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "symbicort", true, "Budesonid", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "entocort", true, "Budesonid", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "budenofalk", true, "Budesonid", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "cortiment", true, "Budesonid", C, QT_TRANSLATE_NOOP("DrugClass", "systemisch"), Cortico },
    { "foster", true, "Beclometason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "ventolair", true, "Beclometason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "junik", true, "Beclometason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "sanasthmax", true, "Beclometason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "trimbow", true, "Beclometason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "flutide", true, "Fluticason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "flutiform", true, "Fluticason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "seretide", true, "Fluticason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "viani", true, "Fluticason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "relvar", true, "Fluticason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "trelegy", true, "Fluticason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "avamys", true, "Fluticason", C, QT_TRANSLATE_NOOP("DrugClass", "nasal"), Cortico },
    { "nasonex", true, "Mometason", C, QT_TRANSLATE_NOOP("DrugClass", "nasal"), Cortico },
    { "asmanex", true, "Mometason", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "alvesco", true, "Ciclesonid", C, QT_TRANSLATE_NOOP("DrugClass", "inhalativ"), Cortico },
    { "dermoxin", true, "Clobetasol", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "dermovate", true, "Clobetasol", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "clarelux", true, "Clobetasol", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "advantan", true, "Methylprednisolon", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "ecural", true, "Mometason", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "dermatop", true, "Prednicarbat", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "ebenol", true, "Hydrocortison", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
    { "ultralan", true, "Fluocortolon", C, QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)"), Cortico },
};

int groupMask(DrugClass::Group g)
{
    switch (g) {
    case DrugClass::Btm:          return B;
    case DrugClass::Pain:         return P;
    case DrugClass::Anticoag:     return A;
    case DrugClass::Antihypertensive: return H;
    case DrugClass::Corticosteroid:   return C;
    default:                      return 0;
    }
}

bool matches(const QString &hay, const Entry &e)
{
    const QString k = QString::fromUtf8(e.key);
    int from = 0;
    while (true) {
        const int i = hay.indexOf(k, from);
        if (i < 0) return false;
        const bool startOk = i == 0 || !hay[i - 1].isLetter();
        const int end = i + int(k.size());
        const bool endOk = !e.brand || end >= hay.size() || !hay[end].isLetter();
        if (startOk && endOk) return true;
        from = i + 1;
    }
}

// Menge des Wirkstoffs je Einheit in mg (aus der Stärke)
std::optional<double> strengthMg(const Medication &m)
{
    const DrugClass::Strength s = DrugClass::parseStrength(m.strength);
    if (!s.ok || s.perTime) return std::nullopt;
    const QString u = s.unit.toLower();
    if (u == QLatin1String("mg")) return s.value;
    if (u == QLatin1String("µg") || u == QLatin1String("mcg") || u == QLatin1String("ug")) return s.value / 1000.0;
    if (u == QLatin1String("g")) return s.value * 1000.0;
    return std::nullopt;
}

} // namespace

namespace DrugClass {

QString groupName(Group g)
{
    switch (g) {
    case Btm:      return tr("Betäubungsmittel (BtM)");
    case Pain:     return tr("Schmerzmittel");
    case Anticoag: return tr("Blutverdünner / Antikoagulanzien");
    case Antihypertensive: return tr("Blutdrucksenker (Antihypertensiva)");
    case Corticosteroid:   return tr("Kortisonhaltige Präparate (Glukokortikoide)");
    default:       return {};
    }
}

QString groupKey(Group g)
{
    switch (g) {
    case Btm:  return QStringLiteral("btm");
    case Pain: return QStringLiteral("pain");
    case Antihypertensive: return QStringLiteral("bp");
    case Corticosteroid:   return QStringLiteral("cortisone");
    default:   return QStringLiteral("anticoag");
    }
}

Hit classifyAuto(const Medication &m, Group g)
{
    Hit h;
    const QString hay = (m.name + QLatin1Char(' ') + m.substance + QLatin1Char(' ') + m.form).toLower();
    const QString hayForm = (m.name + QLatin1Char(' ') + m.form + QLatin1Char(' ') + m.unit).toLower();
    const int want = groupMask(g);
    for (const Entry &e : kEntries) {
        if (!matches(hay, e)) continue;
        const QString subst = substanceName(e.substance);
        switch (e.rule) {
        case None:
            if (!(e.mask & want)) continue;   // gehört nicht zur Gruppe – evtl. weiterer Wirkstoff (Kombination)
            h.match = true;
            h.subclass = QCoreApplication::translate("DrugClass", e.subclass);
            h.reason = subst + QStringLiteral(" – ") + h.subclass
                     + (g == Btm ? tr(" (Anlage III BtMG)") : QString());
            return h;
        case AlwaysUncertain:
            if (!(e.mask & want)) continue;
            h.match = true;
            h.uncertain = true;
            h.subclass = QCoreApplication::translate("DrugClass", e.subclass);
            h.reason = tr("%1 – BtM-Status von der Zubereitung abhängig, bitte prüfen").arg(subst);
            return h;
        case OtherUse:
            if (!(e.mask & want)) continue;
            h.match = true;
            h.uncertain = true;
            h.subclass = QCoreApplication::translate("DrugClass", e.subclass);
            h.reason = tr("%1 – %2; wird auch bei anderen Erkrankungen eingesetzt").arg(subst, h.subclass);
            return h;
        case Cortico: {
            if (!(e.mask & want)) continue;
            // Anwendungsweg: Darreichungsform vor Standard des Eintrags
            static const QList<QPair<QRegularExpression, const char *>> routes = {
                { QRegularExpression(QStringLiteral("augen|ohren|\\beyes?\\b|\\bears?\\b")), QT_TRANSLATE_NOOP("DrugClass", "Augen/Ohren") },
                { QRegularExpression(QStringLiteral("nase|nasal")), QT_TRANSLATE_NOOP("DrugClass", "nasal") },
                { QRegularExpression(QStringLiteral("inhal|dosieraerosol|aerosol|turbohaler|easyhaler|diskus|ellipta|nexthaler|vernebl|nebul")),
                  QT_TRANSLATE_NOOP("DrugClass", "inhalativ") },
                { QRegularExpression(QStringLiteral("salbe|creme|crème|gel\\b|lotion|emulsion|haut|fettsalbe|paste|ointment|cream|topical|skin")),
                  QT_TRANSLATE_NOOP("DrugClass", "äußerlich (Haut)") },
                { QRegularExpression(QStringLiteral("rektal|klysma|zäpfchen|suppos|rectal|enema")), QT_TRANSLATE_NOOP("DrugClass", "rektal") },
                { QRegularExpression(QStringLiteral("tablett|tabl\\b|kapsel|tropfen|saft|injektion|ampulle|infusion|fertigspritze|tablet|capsule|injection")),
                  QT_TRANSLATE_NOOP("DrugClass", "systemisch") },
            };
            QString route = QCoreApplication::translate("DrugClass", e.subclass);
            for (const auto &[rx, name] : routes)
                if (hayForm.contains(rx)) { route = QCoreApplication::translate("DrugClass", name); break; }
            h.match = true;
            h.subclass = route;
            h.reason = tr("%1 – Glukokortikoid, %2").arg(subst, route);
            return h;
        }
        case PerUnitMg: {
            if (!(e.mask & want)) continue;
            h.subclass = QCoreApplication::translate("DrugClass", e.subclass);
            if (g != Btm) {   // als Schmerzmittel immer
                h.match = true;
                h.reason = subst + QStringLiteral(" – ") + h.subclass;
                return h;
            }
            const auto mg = strengthMg(m);
            const QString limit = formatNumber(e.limitMg);
            if (!mg) {
                h.match = true;
                h.uncertain = true;
                h.reason = tr("%1 – BtM erst über %2 mg je Einheit; Stärke unbekannt, bitte prüfen").arg(subst, limit);
            } else if (*mg > e.limitMg) {
                h.match = true;
                h.reason = tr("%1 %2 mg je Einheit – über %3 mg: BtM (Anlage III BtMG)").arg(subst, formatNumber(*mg), limit);
            } else {
                h.reason = tr("%1 bis %2 mg je Einheit – ausgenommene Zubereitung, kein BtM").arg(subst, limit);
            }
            return h;
        }
        case Tilidin: {
            h.subclass = QCoreApplication::translate("DrugClass", e.subclass);
            if (g == Pain) {
                h.match = true;
                h.reason = subst + QStringLiteral(" – ") + h.subclass;
                return h;
            }
            if (g != Btm) continue;
            static const QRegularExpression liquid(QStringLiteral("tropf|lösung|loesung|saft|liquid|drops|solution|syrup"));
            static const QRegularExpression retard(QStringLiteral("retard|\\bret\\b|retardtabl|depot|prolonged|extended|modified.release|\\b(pr|sr|xl)\\b"));
            if (hayForm.contains(liquid)) {
                h.match = true;
                h.reason = tr("%1 als Tropfen/Lösung – BtM (seit 2013)").arg(subst);
            } else if (hayForm.contains(retard)) {
                h.reason = tr("%1 retard mit Naloxon – ausgenommene Zubereitung, kein BtM").arg(subst);
            } else {
                h.match = true;
                h.uncertain = true;
                h.reason = tr("%1 – nur Retardtabletten mit Naloxon sind ausgenommen; Zubereitung bitte prüfen").arg(subst);
            }
            return h;
        }
        case Ass: {
            const auto mg = strengthMg(m);
            const bool low = mg ? *mg <= 160 : (hay.contains(QLatin1String("100")) || hay.contains(QLatin1String("protect"))
                                                || hay.contains(QLatin1String("herz")));
            if (g == Anticoag) {
                if (low) {
                    h.match = true;
                    h.subclass = tr("Thrombozytenaggregationshemmer");
                    h.reason = tr("Acetylsalicylsäure niedrig dosiert – Thrombozytenaggregationshemmer");
                    h.uncertain = !mg.has_value();
                    return h;
                }
                continue;
            }
            if (g == Pain) {
                if (!low) {
                    h.match = true;
                    h.subclass = tr("NSAR");
                    h.reason = tr("Acetylsalicylsäure als Schmerzmittel");
                    h.uncertain = !mg.has_value();
                    return h;
                }
                continue;
            }
            continue;
        }
        }
    }
    // Hinweise aus dem Anwendungsgrund
    const QString reason = m.reason.toLower();
    if (g == Pain) {
        static const QRegularExpression rx(QStringLiteral("schmerz|migräne|migraene|neuralg|kopfweh|pain|migraine|headache|ache\\b"));
        if (reason.contains(rx)) {
            h.match = true;
            h.uncertain = true;
            h.reason = tr("laut Grund/Indikation „%1“").arg(m.reason);
        }
    } else if (g == Antihypertensive) {
        // unbekannte ACE-Hemmer/Sartane an der Wortendung erkennen
        static const QRegularExpression suffix(QStringLiteral("\\b([a-zäöü]{3,}(pril|sartan))\\b"));
        if (const auto mt = suffix.match(hay); mt.hasMatch()) {
            QString w = mt.captured(1);
            w[0] = w[0].toUpper();
            h.match = true;
            h.subclass = mt.captured(2) == QLatin1String("pril") ? QCoreApplication::translate("DrugClass", "ACE-Hemmer")
                                                                  : QCoreApplication::translate("DrugClass", "Sartan (AT1-Blocker)");
            h.reason = w + QStringLiteral(" – ") + h.subclass;
            return h;
        }
        static const QRegularExpression rx(QStringLiteral("hyperton|bluthochdruck|blutdruck|hypertens|high blood pressure"));
        if (reason.contains(rx)) {
            h.match = true;
            h.uncertain = true;
            h.reason = tr("laut Grund/Indikation „%1“").arg(m.reason);
        }
    } else if (g == Anticoag) {
        static const QRegularExpression rx(QStringLiteral("thrombos|embolie|vorhofflimmern|antikoag|gerinnungshemm|blutverdünn"));
        if (reason.contains(rx)) {
            h.match = true;
            h.uncertain = true;
            h.reason = tr("laut Grund/Indikation „%1“").arg(m.reason);
        }
    }
    return h;
}

std::optional<bool> override(const Medication &m, Group g)
{
    const QJsonObject o = m.extra.value(QStringLiteral("classOverride")).toObject();
    const QJsonValue v = o.value(groupKey(g));
    if (v.isBool()) return v.toBool();
    return std::nullopt;
}

void setOverride(Medication &m, Group g, std::optional<bool> value)
{
    QJsonObject o = m.extra.value(QStringLiteral("classOverride")).toObject();
    if (value) o[groupKey(g)] = *value;
    else o.remove(groupKey(g));
    if (o.isEmpty()) m.extra.remove(QStringLiteral("classOverride"));
    else m.extra[QStringLiteral("classOverride")] = o;
}

Hit classify(const Medication &m, Group g)
{
    Hit h = classifyAuto(m, g);
    if (const auto ov = override(m, g)) {
        h.manual = true;
        h.uncertain = false;
        if (*ov && !h.match) h.reason = tr("vom Anwender zugeordnet");
        if (!*ov) h.reason = tr("vom Anwender ausgeschlossen");
        h.match = *ov;
    }
    return h;
}

// --------------------------------------------------------------------- Mengen

QString formatNumber(double v)
{
    QLocale loc;
    if (std::abs(v - std::round(v)) < 1e-9) return loc.toString(qint64(std::llround(v)));
    return loc.toString(v, 'f', std::abs(v * 10 - std::round(v * 10)) < 1e-9 ? 1 : 2);
}

std::optional<double> parseDose(const QString &input)
{
    QString s = input.trimmed();
    if (s.isEmpty() || s == QLatin1String("-") || s == QLatin1String("—")) return std::nullopt;
    s.replace(QStringLiteral("½"), QStringLiteral(" 1/2")).replace(QStringLiteral("¼"), QStringLiteral(" 1/4"))
     .replace(QStringLiteral("¾"), QStringLiteral(" 3/4")).replace(QLatin1Char(','), QLatin1Char('.'));
    s = s.simplified();
    // "1 1/2", "1/2", "1.5"
    static const QRegularExpression rx(QStringLiteral("^(\\d+(?:\\.\\d+)?)?\\s*(?:(\\d+)/(\\d+))?$"));
    const auto mt = rx.match(s);
    if (!mt.hasMatch() || (mt.captured(1).isEmpty() && mt.captured(2).isEmpty())) return std::nullopt;
    double v = mt.captured(1).isEmpty() ? 0 : mt.captured(1).toDouble();
    if (!mt.captured(2).isEmpty() && mt.captured(3).toDouble() > 0) v += mt.captured(2).toDouble() / mt.captured(3).toDouble();
    return v;
}

Strength parseStrength(const QString &input)
{
    Strength st;
    // Tausendertrennzeichen je nach Sprache: deutsch "20.000", englisch "20,000"; Leerzeichen immer
    const bool decimalComma = QLocale().decimalPoint() == QStringLiteral(",");
    const QString groupChars = decimalComma ? QStringLiteral("[ .\\x{00A0}\\x{202F}]") : QStringLiteral("[ ,\\x{00A0}\\x{202F}]");
    const QRegularExpression rx(QStringLiteral("(\\d{1,3}(?:") + groupChars + QStringLiteral("\\d{3})+(?![.,]?\\d)|\\d+(?:[.,]\\d+)?)"
        "\\s*(mg|µg|μg|mcg|ug|g|ie|i\\.e\\.|iu|i\\.u\\.|e|u|ml|%|mmol)?"
        "(\\s*/\\s*(?:\\d+\\s*)?([a-zäöü]+))?"), QRegularExpression::CaseInsensitiveOption);
    const auto mt = rx.match(input);
    if (!mt.hasMatch()) return st;
    QString num = mt.captured(1);
    if (num.contains(QRegularExpression(groupChars + QStringLiteral("\\d{3}$"))))
        num.remove(QRegularExpression(groupChars));
    num.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    st.value = num.toDouble(&ok);
    st.unit = mt.captured(2);
    if (st.unit == QStringLiteral("μg")) st.unit = QStringLiteral("µg");
    if (st.unit.compare(QLatin1String("i.e."), Qt::CaseInsensitive) == 0 || st.unit.compare(QLatin1String("ie"), Qt::CaseInsensitive) == 0)
        st.unit = QStringLiteral("IE");
    if (st.unit.compare(QLatin1String("i.u."), Qt::CaseInsensitive) == 0 || st.unit.compare(QLatin1String("iu"), Qt::CaseInsensitive) == 0)
        st.unit = QStringLiteral("IU");
    // "/h", "/ml", "/Hub" … = Konzentration bzw. Abgaberate, kein Gehalt je Einheit
    st.perTime = !mt.captured(3).isEmpty();
    st.ok = ok && !st.unit.isEmpty();
    return st;
}

DailyAmount dailyAmount(const Medication &m)
{
    DailyAmount d;
    double sum = 0;
    bool any = false, unparsed = false;
    for (const QString &x : { m.morning, m.noon, m.evening, m.night }) {
        const QString t = x.trimmed();
        if (t.isEmpty() || t == QLatin1String("0")) continue;
        if (const auto v = parseDose(t)) { sum += *v; any = true; }
        else unparsed = true;
    }
    if (any && !unparsed) {
        d.units = sum;
        const QString unit = m.unit.isEmpty() ? tr("Stück") : m.unit;
        d.unitsText = formatNumber(sum) + QLatin1Char(' ') + unit;
        // Wirkstoffmenge: Einheiten × Stärke (nur bei abgeteilten Formen)
        const Strength st = parseStrength(m.strength);
        static const QRegularExpression countUnit(QStringLiteral(
            "^(stück|stk|tabl|tablette|tabletten|filmtabl|kapsel|kapseln|kps|dragee|hub|hübe|sprühstoß|beutel|"
            "zäpfchen|supp|ampulle|amp|fertigspritze|pen|lutschtabl|schmelztabl|pflaster|tbl|"
            "pcs|pc|piece|pieces|tab|tabs|tablet|tablets|cap|caps|capsule|capsules|puff|puffs|spray|sprays|"
            "sachet|sachets|suppository|suppositories|ampoule|ampoules|syringe|syringes|pre-filled syringe|"
            "patch|patches|lozenge|lozenges)\\.?$"),
            QRegularExpression::CaseInsensitiveOption);
        if (st.ok && !st.perTime && countUnit.match(unit.trimmed()).hasMatch())
            d.activeText = formatNumber(sum * st.value) + QLatin1Char(' ') + st.unit;
        else if (st.ok && unit.trimmed().compare(st.unit, Qt::CaseInsensitive) == 0)
            d.activeText = formatNumber(sum) + QLatin1Char(' ') + st.unit;
    }
    switch (m.category) {
    case Medication::AsNeeded:  d.perText = tr("je Bedarfsfall"); break;
    case Medication::Emergency: d.perText = tr("je Notfallgabe"); break;
    case Medication::Other:     d.perText = tr("laut Plan"); break;
    default:
        d.perText = MedSchedule::isDaily(m) ? tr("pro Tag")
                                            : tr("pro Einnahmetag (%1)").arg(MedSchedule::describe(m));
    }
    return d;
}

} // namespace DrugClass
