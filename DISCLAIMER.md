# Legal Disclaimer & Compliance Notice

**Project:** AdvHD Modernization Suite — Decoupled JXL & AV1 Plugins
**Origin of publication:** Canada
**Applies to:** all users, with specific guidance for users located in **mainland China**
**Language of record:** English (this version is authoritative)

---

> **This document is not legal advice.** It is a good-faith disclosure of the legal basis on which
> this project is published and of the obligations that fall on you as a user. No lawyer–client or
> solicitor–client relationship is created by reading it or by using this project. Laws differ by
> country and change over time. If your situation is unclear, consult a qualified lawyer in your
> own jurisdiction **before** you download, build, or run anything here.

---

## 1. Compliance with Canadian law

This project is developed, maintained, and published **from Canada**, and it is published on the
basis that it complies with applicable Canadian law, including the *Copyright Act*, R.S.C., 1985,
c. C-42. In particular:

* **Interoperability.** Canadian law provides that it is not an infringement of copyright in a
  computer program for a person who owns an authorized copy of that program — or holds a licence to
  use a copy — to reproduce the copy for the sole purpose of obtaining the information needed to
  make that program and another program interoperable, subject to limits on how that information may
  be used or disclosed (*Copyright Act*, [s. 30.61](https://laws-lois.justice.gc.ca/eng/acts/C-42/section-30.61.html),
  "Interoperability of computer programs"). The reverse-engineering notes in this repository
  document the interfaces, calling conventions, and media-format constraints required to make the
  AdvHD engine interoperate with modern codecs.
* **Technological protection measures.** This project does **not** descramble, decrypt, bypass,
  remove, deactivate, or impair any technological protection measure within the meaning of
  *Copyright Act* [s. 41](https://laws-lois.justice.gc.ca/eng/acts/C-42/section-41.html). It ships no
  cracked, pre-patched, or key-generating material, and it is not intended to enable access to any
  work that you are not already entitled to access.
* **No redistribution of protected works.** No third-party creative work is redistributed here
  (see §3 and §5).

The summaries above are abbreviated and are provided for transparency. They are not a substitute for
the statute, and they are not a legal opinion about your particular use.

---

## 2. Notice to users in mainland China

This project is offered from Canada under Canadian law. **Regardless of where the project is
published, your use of it happens where you are**, and you are solely responsible for complying
with the laws that apply to you.

If you are located in **mainland China**, please note the following.

1. **A different legal regime applies to you.** Your use is governed principally by the laws of the
   People's Republic of China, which are not the same as Canadian law. Relevant instruments include,
   among others, the **Copyright Law of the People's Republic of China**, the **Regulations on
   Computer Software Protection**, the **Civil Code of the People's Republic of China**, and
   applicable judicial interpretations.
2. **Do not assume Canadian permissions travel with you.** The interoperability exception described
   in §1 is a feature of *Canadian* law. The laws of the PRC do not necessarily contain an
   equivalent exception, and acts that are lawful in Canada may be unlawful — civilly or criminally —
   in mainland China. Nothing in this repository should be read as a representation that any
   particular act is permitted under PRC law.
3. **Own a lawful copy.** Use this project only with a game copy that you have lawfully acquired and
   are licensed to use. Do not use it with pirated, cracked, or otherwise unauthorized copies.
4. **Do not distribute protected content.** Do not use this project to extract, repackage, upload,
   or distribute game assets, artwork, audio, video, scripts, or executables belonging to any rights
   holder. Converting a file for your own private use is not a licence to publish it.
5. **Network access is your responsibility.** Obtaining the optional third-party components listed
   in §5 requires access to external services (for example GitHub and vendor download sites). You
   are responsible for complying with any network, telecommunications, and content rules that apply
   to you.
6. **Obtain local advice.** If you intend to use, modify, or redistribute this project in mainland
   China — particularly for any commercial or public purpose — consult qualified PRC counsel first.

---

## 3. No game content is distributed; no affiliation

This repository contains **only original source code and documentation authored by its
contributors**. It does **not** contain, bundle, mirror, or distribute:

* any game executable, patch, or crack;
* any game asset — artwork, sprites, backgrounds, audio, video, scripts, or archives;
* any encryption or decryption keys;
* any third-party binary library.

This project is **not affiliated with, authorized by, endorsed by, or sponsored by** WillPlus,
RioShiina, or any other developer, publisher, or rights holder of any AdvHD Engine title. All game
titles and their assets remain the property of their respective rights holders. You must obtain your
own lawful copy of any game you use this project with.

---

## 4. Accepted use

This project is intended for:

* **Personal interoperability and format modernization** — enabling a game you lawfully own to play
  modern AV1/Opus video and JPEG XL textures, and to reduce storage footprint, on your own hardware.
* **Security and preservation research**, study, and teaching.
* **Interoperability study** consistent with the exception described in §1.

It is **not** intended for, and you must not use it for:

* circumventing copy protection, licence checks, or digital rights management;
* distributing, selling, or publicly performing game content you do not own or license;
* creating or distributing pirated copies of any software;
* any unlawful, deceptive, or infringing purpose.

---

## 5. Third-party components (obtained separately, not distributed here)

This project **dynamically loads** the following at runtime and does **not** bundle, link, or
redistribute them. Each remains governed exclusively by its own licence, and **you** are responsible
for reviewing and complying with that licence before obtaining or using it:

| Component | Purpose | Typical licence |
| :--- | :--- | :--- |
| **LAV Filters** (`LAVSplitter.ax`, `LAVVideo.ax`, `LAVAudio.ax` and dependencies) | Registration-free DirectShow decoding for AV1/Opus | GPL-2.0-or-later |
| **libjxl** (`libjxl.dll`, `libjxl_cms.dll` and dependencies) | JPEG XL texture decoding | BSD-3-Clause |
| **HandBrakeCLI** | Offline video transcoding (SVT-AV1 / Opus) | GPL-2.0-or-later |
| **MinGW-w64 runtime** (`libgcc`, `libstdc++`) | Statically linked build runtime | GPL-3.0-or-later with GCC Runtime Library Exception |

The build scripts compile against publicly available C/C++ **headers** only. No third-party library
is statically linked into this project's outputs; every third-party symbol is resolved at runtime
through `LoadLibrary`/`GetProcAddress`. Redistributing this project together with those components
may impose additional obligations on **you** — in particular, the GPL components carry source- and
notice-availability requirements. Distributing them is your decision and your responsibility.

---

## 6. Modifying your game installation is at your own risk

This software injects code into a running process and rewrites asset containers. Such operations can
corrupt files, break saved games, destabilize the host application, trigger anti-tamper or
anti-cheat mechanisms, or violate the terms of service of a platform or storefront. Back up your
installation first. Do not use this project where doing so would breach an agreement you are party
to, or where it would disrupt a multiplayer or online service.

---

## 7. No warranty; limitation of liability

This project is licensed under the **MIT License** (see [`LICENSE`](LICENSE)). It is provided
**"AS IS"**, without warranty of any kind, express or implied, including but not limited to the
warranties of merchantability, fitness for a particular purpose, and non-infringement. In no event
shall the authors or copyright holders be liable for any claim, damages, or other liability, whether
in an action of contract, tort, or otherwise, arising from, out of, or in connection with the
software or its use.

**You assume the entire risk** of downloading, building, installing, and running this software, and
of any consequence to your game installation, your data, your hardware, or your legal standing.

---

## 8. Trademarks

All product names, game titles, company names, logos, and trademarks referenced in this repository
are the property of their respective owners and are used for identification and descriptive purposes
only. Their use does not imply any affiliation with or endorsement by the trademark owners.

---

## 9. Rights holders: contact and takedown

If you are a rights holder and you believe this repository infringes your rights, please open an
issue on this repository (or contact the maintainer through the repository's published contact
channel) with the specific material at issue and the basis for your concern. Good-faith requests
will be reviewed promptly, and material will be removed or amended where warranted. This project
distributes no game content and has no interest in hosting anything that infringes another party's
rights.

---

*Last updated: 2026-10-06*
