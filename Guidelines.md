# Guidelines for LAIK

To be used after v0.1

## Versioning

LAIK versions look like "major.minor.bugfix", with
* major: different numbers mean source and/or binary incompatible API changes
* minor: different minor number with same major means that new features are
  added, but programs compiled with old version of same major number work
* bugfix: if major/minor stays some, but only bugfix changes, no API/ABI
  changes are done, but just bug fixes

## Release Process

The master branch always points to the previous major or minor release,
and dev is the branch worked on. For new releases, dev gets merged into
master.

On a release:
* add changes since last release to Changelog-Version to doc/
* bump version number
* merge dev into master
* build release tar ball, including
  * sources
  * built HTML/PDF documentation
  * run doxygen
* update website

## Contributor Rules

* roles
  * regular developer
  * core contributor / maintainer

* implementations of public API functions must be documented in doxygen syntax
  * what is the function about
  * description of input / output parameters
  * error condition
  
* new features
  * must be developed in a feature branch, named "contributor initials/feature"
  * only to be merged into "dev" (= development branch for next release)
  * a test for the feature must exist and be part of the test suite
  * make sure the test works in Travis (not just on your developer laptop)
  * generated files (also from test runs) should be excluded via .gitignore, and
    must be deleted by "make clear"
  * changes must include a description of the new feature in the adequate manual section
  
* Reviews are done by using Github PRs

* API changes must be reviewed before merge by at least one core contributor

* before merging, the feature must work on current "dev" top, ie. the feature branch must be rebased to "dev" with Travis suceeding, always resulting in fast-forward merges

* bug fixes must be reviewed, and should include a test which shows that the bug indeed is fixed

* Commit Message
  * prefix with module name where most changes are done, e.g. "[core] "
  * mark API changes with [API]
  * start bug fixes with "Fix ..." and if this relates to a Github issue, reference the issue number
  * a commit message must include a short summary of what the commit is about

## Header Files and Include Directives

Header files in LAIK are subject to the following rules; the keywords are to be
interpreted according to [RFC 2119](https://www.ietf.org/rfc/rfc2119.txt):

  1. Files and directories below ```include``` MUST begin with ```laik```.
  2. When building liblaik, both ```include``` and ```src``` MUST be in the
     include path.
  3. When building other parts of the LAIK project, ```include``` MUST be in
     the include path and ```src``` MUST NOT be in the include path.
  4. ```include "foo.h"``` MUST NOT be used if ```foo.h``` is not in the same
     directory.
