# Provenance

## Included material

| Material | Origin and scope |
| --- | --- |
| Container tools | Independently written project implementation of observed record framing and checksum conventions. No vendor source or external patcher source is included. |
| Research map | Selected addresses, relationships, field descriptions and evidence hashes from analysis of original KeyStep 37 firmware 1.1.6.579. Names are provisional. |
| Device extension | The project's event-driven generator and hardware adapters from the frozen generator14 source set. `device/source-manifest.json` identifies each included C/Thumb/header file. Short version-specific hooks preserve displaced native instructions; vendor images are not bundled. |
| Tests and documentation | Project-authored synthetic fixtures, tests and technical descriptions. Vendor firmware is supplied locally by the reader when required. |

## External research

The earlier checksum investigation consulted [auduchinok's KeyStep 37 patcher](https://gist.github.com/auduchinok/1dea3290af548be0a56767f9957fbadc), revision `60a4cc897c328d7a08a7e2039d548994f97f0ba3`, file `ledtobin37_patch_only.py`. Its SHA-256 was `c21f27a4963da83a0207f06f637c26d83d5a3672f77691a091b6c35a0a42834d`. It independently corroborated the FF-gap and final LE16 sum convention. That source was read, not imported or executed; no code or patch bytes from it are redistributed. Its earlier reference is [Daniel Gruss's KeyStep research](https://dsgruss.com/notes/2020/10/02/keystep1.html); the original KeyStep has different geometry.

The [official KeyStep 37 manual v1.1](https://dl.arturia.net/products/keystep-37/manual/keystep-37_Manual_1_1_EN.pdf) describes the native controls and modes. It is available from the vendor and is not bundled here.
