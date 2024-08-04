# spec file for package far2l

%define	base_Name far2l
%define	has_copyright	 0

# Options to enable / disable extra plug-ins

%define	use_python_plugin	1
%define	use_lua_plugin		1
%define use_netcfg			1
%define use_jumpword		1
%define use_editwrap		1
%define use_sqlitep			1
%define use_procp			1
%define use_metapackages	1

# Standard header

Name:           %{base_Name}
Version:        2.6.3
Release:        217
Group:			Productivity/File utilities
Summary:        Far manager for Linux
License:        GPL-2.0-only
URL:            https://github.com/elfmz/far2l.git
Source0:        %{base_Name}-core_%{version}.orig.tar.xz
Source1:        changelog
BuildArchitectures: x86_64 i586 aarch64
Distribution: 	openSUSE Tumbleweed

%if 0%{?is_opensuse}
	%if 0%{?sle_version} == 150300
BuildRequires:  gcc10 gcc10-c++
	%define override_gcc -DCMAKE_CXX_COMPILER=g++-10
	%endif
	%if 0%{?sle_version} == 150400
BuildRequires:  gcc11 gcc11-c++
	%define override_gcc -DCMAKE_CXX_COMPILER=g++-11
	%endif
	%if 0%{?sle_version} == 150500
BuildRequires:  gcc12 gcc12-c++
	%define override_gcc -DCMAKE_CXX_COMPILER=g++-12
	%endif
	%if 0%{?sle_version} == 150600
BuildRequires: gcc13 gcc13-c++
	%define override_gcc -DCMAKE_CXX_COMPILER=g++-13
	%endif

	%if 0%{?suse_version} > 1600
BuildRequires:  gcc-c++
	%endif
%else
BuildRequires:  gcc-c++
%endif

BuildRequires:  pkgconfig
BuildRequires:  gawk m4 make git
BuildRequires:  cmake

%if %{defined suse_version}
BuildRequires: 	wxGTK3-3_2-devel 
BuildRequires: 	libxerces-c-devel libneon-devel libopenssl-devel libuchardet-devel
BuildRequires:	update-desktop-files
%endif
%if 0%{?fedora} || 0%{?rhel}
BuildRequires:  wxGTK-devel >= 3.1
BuildRequires: 	xerces-c-devel neon-devel openssl-devel uchardet-devel
%endif

BuildRequires:  spdlog-devel fmt-devel libarchive-devel 
BuildRequires:  libnfs-devel libsmbclient-devel libssh-devel pcre-devel
BuildRequires:  python-rpm-macros
BuildRequires:	libicu-devel

# BuildRequires:	wxWidgets-devel >= 3.1

Requires:		/bin/sh

%description
Far 2 Linux is enhanced port of the well-known dual-panel console file manager from the Windows world. Despite to the original, 
the far 2 Linux has started from the nearly original code from Eugene Roshal and it is based upon FAR 2 codebase but not FAR 3. 

In addition, Linux version supports both X11 and console modes, with over-ssh access to the clipboard and fully-working keyboard 
rather than ancient termcap limitations. It introduces background operations for copying, live console scrolling and many other 
features have implemented in Linux version. Couple of well-known plug-ins are available out of the box, including 
Colorer, archiving, and network operations.

# per-package separation

%package core
Summary: 	Far for Linux main code, works with terminals
Suggests:	far2l-plugins-ttyxi far2l-wxgtk far2l-plugins-netrocks-ftp far2l-plugins-netrocks-sftp far2l-plugins-netrocks-smb 
Suggests:	far2l-plugins-netrocks-webdav far2l-plugins-netrocks-nfs far2l-plugins-netrocks-shell
Suggests:	tar gzip bzip2 exiftool 7zip zstd elfutils gpg2 util-linux
#Requires:	/bin/bash 
#Requires:	libarchive13
#Requires:	libfmt9 
#Requires:	libuchardet0 libxerces-c-3_2 libpcre1
Conflicts:	far2l-full
Provides:	far2l

%description core
Far for Linux main files with core plug-ins, needs terminal to work

Far 2 Linux is enhanced port of the well-known dual-panel console file manager from the Windows world. Despite to the original, 
the far 2 Linux has started from the nearly original code from Eugene Roshal and it is based upon FAR 2 codebase but not FAR 3. 

In addition, Linux version supports both X11 and console modes, with over-ssh access to the clipboard and fully-working keyboard 
rather than ancient termcap limitations. It introduces background operations for copying, live console scrolling and many other 
features have implemented in Linux version. Couple of well-known plug-ins are available out of the box, including 
Colorer, archiving, and network operations.

#---

%package plugins-ttyxi
Summary: Far for Linux enhanced keyboard support for terminals
#Requires:	libXi6
Requires: 	far2l-core
Conflicts:	far2l-full, far2l-ttyxi

%description plugins-ttyxi
Far for Linux extension to support enhanced keyboard (for case it have started inside X session)

#---

%package wxgtk
Summary: Far for Linux UI for X11
#Requires: 	libwx_baseu-suse5_0_0 libwx_gtk3u_core-suse5_0_0 
# wxWidgets > 3.0 ?
#BuildRequires: 	wxGTK3-3_2-devel 
%if 0%{?fedora} || 0%{?rhel}
BuildRequires:  wxGTK-devel >= 3.1
%endif
Requires: 	far2l-core
Conflicts:	far2l-full

%description wxgtk
Far for Linux UI for X11 on top of wxWidgets and GTK

#---

%package plugins-netrocks-ftp
Summary: Far for Linux plug-in to communicate via FTP
#Requires: 	libopenssl1_1 
Requires: 	far2l-core
Conflicts:	far2l-full

%description plugins-netrocks-ftp
Far for Linux plug-in to communicate via FTP, as part of NetRocks

%package plugins-netrocks-sftp
Summary: Far for Linux plug-in to communicate via SFTP
#Requires: 	libssh2-1 libopenssl1_1
Requires: 	far2l-core
Conflicts:	far2l-full

%description plugins-netrocks-sftp
Far for Linux plug-in to communicate via sFTP, as part of NetRocks

#---

%package plugins-netrocks-smb
Summary: Far for Linux plug-in to communicate via SMB
#Requires: 	libsmbclient0
Requires: 	far2l-core
Conflicts:	far2l-full

%description plugins-netrocks-smb
Far for Linux plug-in to communicate via SMB, as part of NetRocks

%package plugins-netrocks-webdav
Summary: Far for Linux plug-in to communicate via WebDAV
#Requires: 	libneon27
Requires: 	far2l-core
Conflicts:	far2l-full

%description plugins-netrocks-webdav
Far for Linux plug-in to communicate via WebDAV, as part of NetRocks

#---

%package plugins-netrocks-nfs
Summary: Far for Linux plug-in to communicate via NFS
#Requires: 	libnfs13
Requires: 	far2l-core
Conflicts:	far2l-full

%description plugins-netrocks-nfs
Far for Linux plug-in to communicate via NFS, as part of NetRocks

%package plugins-netrocks-shell
Summary: Far for Linux plug-in to communicate via FISH
Requires: 	far2l-core
Conflicts:	far2l-full

%description plugins-netrocks-shell
Far for Linux plug-in to communicate via FISH protocol, as part of NetRocks

# and full
#---

%if %{use_metapackages}

%package server
Summary: Far for Linux (server-ready installation)
Requires:	far2l-core far2l-plugins-netrocks-ftp far2l-plugins-netrocks-sftp far2l-plugins-netrocks-shell
Recommends:	tar gzip bzip2 exiftool 7zip zstd elfutils gpg2 util-linux
Suggests:	chafa jp2a pandoc-cli poppler-tools colordiff catdoc catppt ctags gptfdisk rpm dpkg 
Suggests:	far2l-plugins-netrocks-smb far2l-plugins-netrocks-webdav far2l-plugins-netrocks-nfs far2l-plugins-ttyxi 
%if %{use_netcfg}
Suggests:	far2l-netcfg
%endif

%description server
Far 2 Linux is enhanced port of the well-known dual-panel console file manager from the Windows world. Despite to the original, 
the far 2 Linux has started from the nearly original code from Eugene Roshal and it is based upon FAR 2 codebase but not FAR 3. 

In addition, Linux version supports both X11 and console modes, with over-ssh access to the clipboard and fully-working keyboard 
rather than ancient termcap limitations. It introduces background operations for copying, live console scrolling and many other 
features have implemented in Linux version. Couple of well-known plug-ins are available out of the box, including 
Colorer, archiving, and network operations.

Use desktop edition to work comfortably; the server edition is minimal set of the functions without GUI and extra plug-ins.

%package desktop
Summary: Far for Linux (desktop-ready installation)
Requires:	far2l-core far2l-wxgtk far2l-plugins-ttyxi far2l-plugins-netrocks-ftp far2l-plugins-netrocks-sftp far2l-plugins-netrocks-shell 
Requires:	far2l-plugins-netrocks-smb far2l-plugins-netrocks-webdav far2l-plugins-netrocks-nfs
Recommends:	tar gzip bzip2 exiftool 7zip zstd elfutils gpg2 util-linux
Recommends:	chafa jp2a pandoc-cli poppler-tools colordiff catdoc catppt ctags gptfdisk rpm dpkg 
%if %{use_netcfg}
Recommends:	far2l-plugins-netcfg
%endif
%if %{use_sqlitep}
Recommends:	far2l-plugins-sqlite
%endif
%if %{use_procp}
Recommends:	far2l-plugins-processes
%endif
%if %{use_python_plugin}
Suggests:	far2l-plugins-python
%endif
%if %{use_python_plugin}
Suggests:	far2l-plugins-lua
%endif


%description desktop
Far 2 Linux is enhanced port of the well-known dual-panel console file manager from the Windows world. Despite to the original, 
the far 2 Linux has started from the nearly original code from Eugene Roshal and it is based upon FAR 2 codebase but not FAR 3. 

In addition, Linux version supports both X11 and console modes, with over-ssh access to the clipboard and fully-working keyboard 
rather than ancient termcap limitations. It introduces background operations for copying, live console scrolling and many other 
features have implemented in Linux version. Couple of well-known plug-ins are available out of the box, including 
Colorer, archiving, and network operations.

Use server edition if you need minimak console-only subset of functions.

%else

%package full
Summary: Far for Linux (complete installation)
#Requires: 		/bin/sh libarchive13 libopenssl1_1 libfmt8 libnfs13 libssh2-1 libuchardet0 libxerces-c-3_2 libneon27 
#Requires: 		libsmbclient0 libpcre2-32-0 libwx_baseu-suse5_0_0 libwx_gtk3u_core-suse5_0_0 
#Requires:		python3
Conflicts:	far2l-core far2l-plugins-ttyxi far2l-wxgtk far2l-plugins-netrocks-ftp far2l-plugins-netrocks-sftp far2l-plugins-netrocks-smb far2l-plugins-netrocks-webdav far2l-plugins-netrocks-nfs far2l-plugins-netrocks-shell
Provides:	far2l
Recommends:	tar gzip bzip2 exiftool 7zip zstd elfutils gpg2 util-linux
Suggests:	chafa jp2a pandoc-cli poppler-tools colordiff catdoc catppt ctags gptfdisk rpm dpkg

%description full
Far 2 Linux is enhanced port of the well-known dual-panel console file manager from the Windows world. Despite to the original, 
the far 2 Linux has started from the nearly original code from Eugene Roshal and it is based upon FAR 2 codebase but not FAR 3. 

In addition, Linux version supports both X11 and console modes, with over-ssh access to the clipboard and fully-working keyboard 
rather than ancient termcap limitations. It introduces background operations for copying, live console scrolling and many other 
features have implemented in Linux version. Couple of well-known plug-ins are available out of the box, including 
Colorer, archiving, and network operations.

%endif

# netcfg

%if %{use_netcfg}
%package plugins-netcfg
Summary: Far for Linux plug-in to manage network interfaces
Requires: 		far2l

%description plugins-netcfg
Far for Linux plug-in to manage network interfaces, separate plug-in with MIT license from https://github.com/VPROFi/netcfgplugin
%endif

%if %{use_sqlitep}
%package plugins-sqlite
Summary: Far for Linux plug-in to manage SQLite databases
Requires: 		far2l
%if 0%{?fedora} || 0%{?rhel}
Requires: 		libsqlite3x
BuildRequires: 	libsqlite3x-devel
%else
Requires: 		libsqlite3-0
BuildRequires: 	sqlite3-devel
%endif

%description plugins-sqlite
Far for Linux plug-in to manage SQLite files, separate plug-in with MIT license from https://github.com/VPROFi/sqlplugin
%endif

%if %{use_procp}
%package plugins-processes
Summary: Far for Linux plug-in to manage Linux processes
Requires: 		far2l

%description plugins-processes
Far for Linux plug-in to manage Linux processes, separate plug-in with MIT license from https://github.com/VPROFi/processes
%endif

# python

%if %{use_python_plugin}
%package plugins-python
Requires:		far2l
%if 0%{?is_opensuse}
%if 0%{?suse_version} > 1600
Requires:		python312  python312-cffi python312-pip
BuildRequires:  python312 python312-devel python312-cffi python312-pip
%else
Requires:		python3  python3-cffi python3-pip
BuildRequires:  python3 python3-devel python3-cffi python3-pip
%endif
%else
Requires:		python3  python3-cffi python3-pip
BuildRequires:  python3-devel python3-cffi python3-pip
%endif
# Dirty hack for Factory/TW: it installs python 3.10 and 3.11 but python-devel 3.10 only

Summary: Far for Linux plug-in to support Python plug-ins

%description plugins-python
Far for Linux plug-in to support Python plug-ins, with extra samples inside
%endif

# lua

%if %{use_lua_plugin}
%package plugins-lua
Requires:		far2l
Requires:		lua51 luajit
BuildRequires:  lua51-devel luajit-devel

Summary: Far for Linux plug-in to support Lua plug-ins

%description plugins-lua
Far for Linux plug-in to support Lua plug-ins
%endif

# extra editor plug-ins

%if %{use_jumpword}
%package plugins-jumpword
Requires:		far2l

Summary: Far for Linux plug-in to navigate the definition of the word under cursor, like IDE doing

%description plugins-jumpword
Far for Linux plug-in to navigate the definition of the word under cursor (in forward and backward direction), like IDE doing. 
%endif

%if %{use_editwrap}
%package plugins-editwrap
Requires:		far2l

Summary: Far for Linux editor plug-in to wrap long lines

%description plugins-editwrap
Far for Linux editor plug-in to wrap long lines. Warning: it is sample plug-in and it uses whole text editor area despite of the selection!
%endif

# build

%prep
%autosetup -p1

%build
mkdir build
pushd build
cmake -DCMAKE_INSTALL_PREFIX:PATH=%{buildroot}%{_prefix} \
	%{override_gcc} \
	-DDPROCPLUGINMACROFUNC=1 \
%if %{use_netcfg}
	-DNETCFG=yes \
%else
	-DNETCFG=no \
%endif
	-DUSEWX=yes \
%if %{use_python_plugin}
	-DVIRTUAL_PYTHON=python3 \
%else
	-DPYTHON=no \
%endif
%if %{use_python_plugin}
	-DLUAFAR=yes \
%else
	-DLUAFAR=no \
%endif
%if %{use_jumpword}
	-DEDITOR_JUMPWORD=yes \
%else
	-DEDITOR_JUMPWORD=no \
%endif
%if %{use_editwrap}
	-DEDITOR_EDITWRAP=yes \
%else
	-DEDITOR_EDITWRAP=no \
%endif
%if %{use_sqlitep}
	-DSQLITEPLUGIN=yes \
%else
	-DSQLITEPLUGIN=no \
%endif
%if %{use_procp}
	-DPROCPLUGIN=yes \
%else
	-DPROCPLUGIN=no \
%endif
	-DCMAKE_BUILD_TYPE=Release ..
make
popd

%install
pushd build
make install
popd
%if %{defined suse_version}
# workaround with wrong link to far2ledit
rm %{buildroot}%{_prefix}/bin/far2ledit
ln -s %{_prefix}/bin/far2l %{buildroot}%{_prefix}/bin/far2ledit

%suse_update_desktop_file %{base_Name}
%endif
%if 0%{?fedora} || 0%{?rhel}

%endif
%if %{defined debian}

%endif

%post
%if %{defined suse_version}
%suse_update_desktop_file -r %{base_Name} System Utility
%endif
if [ -x /usr/bin/update-desktop-database ]; then
	/usr/bin/update-desktop-database > /dev/null || :
fi

%postun
if [ -x /usr/bin/update-desktop-database ]; then
	/usr/bin/update-desktop-database > /dev/null || :
fi

# per-package files

%files core
%license LICENSE.txt

%_bindir/%{base_Name}
%_bindir/far2ledit
%dir %{_prefix}/lib/%{base_Name}
%{_prefix}/lib/%{base_Name}/%{base_Name}_askpass
%{_prefix}/lib/%{base_Name}/%{base_Name}_sudoapp

%dir %_datadir/%{base_Name}
%dir %_datadir/%{base_Name}/Plugins
%dir %_datadir/%{base_Name}/Plugins/align
%dir %_datadir/%{base_Name}/Plugins/autowrap
%dir %_datadir/%{base_Name}/Plugins/calc
%dir %_datadir/%{base_Name}/Plugins/colorer
%dir %_datadir/%{base_Name}/Plugins/compare
%dir %_datadir/%{base_Name}/Plugins/drawline
%dir %_datadir/%{base_Name}/Plugins/editcase
%dir %_datadir/%{base_Name}/Plugins/editorcomp
%dir %_datadir/%{base_Name}/Plugins/filecase
%dir %_datadir/%{base_Name}/Plugins/incsrch
%dir %_datadir/%{base_Name}/Plugins/inside
%dir %_datadir/%{base_Name}/Plugins/multiarc
%dir %_datadir/%{base_Name}/Plugins/SimpleIndent
%dir %_datadir/%{base_Name}/Plugins/NetRocks
%dir %_datadir/%{base_Name}/Plugins/NetRocks/plug
%dir %_datadir/%{base_Name}/Plugins/tmppanel
%_datadir/%{base_Name}/*.*

%_datadir/%{base_Name}/Plugins/align/*
%_datadir/%{base_Name}/Plugins/autowrap/*
%_datadir/%{base_Name}/Plugins/calc/*
%_datadir/%{base_Name}/Plugins/colorer/*
%_datadir/%{base_Name}/Plugins/compare/*
%_datadir/%{base_Name}/Plugins/drawline/*
%_datadir/%{base_Name}/Plugins/editcase/*
%_datadir/%{base_Name}/Plugins/editorcomp/*
%_datadir/%{base_Name}/Plugins/filecase/*
%_datadir/%{base_Name}/Plugins/incsrch/*
%_datadir/%{base_Name}/Plugins/inside/*
%_datadir/%{base_Name}/Plugins/multiarc/*
%_datadir/%{base_Name}/Plugins/SimpleIndent/*
%_datadir/%{base_Name}/Plugins/NetRocks/plug/*.*
%_datadir/%{base_Name}/Plugins/tmppanel/*

%_datadir/applications/%{base_Name}.desktop

%_datadir/icons/%{base_Name}.svg
%if ( 0%{?sle_version} <= 150600 ) && 0%{?is_opensuse}
%dir %_datadir/icons/hicolor/1024x1024
%dir %_datadir/icons/hicolor/1024x1024/apps
%endif
%_datadir/icons/hicolor/1024x1024/apps/%{base_Name}.svg
%_datadir/icons/hicolor/128x128/apps/%{base_Name}.svg
%_datadir/icons/hicolor/16x16/apps/%{base_Name}.svg
%_datadir/icons/hicolor/192x192/apps/%{base_Name}.svg
%_datadir/icons/hicolor/24x24/apps/%{base_Name}.svg
%_datadir/icons/hicolor/256x256/apps/%{base_Name}.svg
%_datadir/icons/hicolor/32x32/apps/%{base_Name}.svg
%_datadir/icons/hicolor/48x48/apps/%{base_Name}.svg
%_datadir/icons/hicolor/512x512/apps/%{base_Name}.svg
%_datadir/icons/hicolor/64x64/apps/%{base_Name}.svg
%_datadir/icons/hicolor/72x72/apps/%{base_Name}.svg
%_datadir/icons/hicolor/96x96/apps/%{base_Name}.svg

%{_prefix}/share/man/man1/*
%{_prefix}/share/man/ru/man1/*
%if %{has_copyright}
%{_prefix}/share/doc/far2l/copyright
%endif

%dir %{_prefix}/lib/%{base_Name}/Plugins
%dir %{_prefix}/lib/%{base_Name}/Plugins/align
%dir %{_prefix}/lib/%{base_Name}/Plugins/autowrap
%dir %{_prefix}/lib/%{base_Name}/Plugins/calc
%dir %{_prefix}/lib/%{base_Name}/Plugins/colorer
%dir %{_prefix}/lib/%{base_Name}/Plugins/compare
%dir %{_prefix}/lib/%{base_Name}/Plugins/drawline
%dir %{_prefix}/lib/%{base_Name}/Plugins/editcase
%dir %{_prefix}/lib/%{base_Name}/Plugins/editorcomp
%dir %{_prefix}/lib/%{base_Name}/Plugins/filecase
%dir %{_prefix}/lib/%{base_Name}/Plugins/incsrch
%dir %{_prefix}/lib/%{base_Name}/Plugins/inside
%dir %{_prefix}/lib/%{base_Name}/Plugins/multiarc
%dir %{_prefix}/lib/%{base_Name}/Plugins/NetRocks
%dir %{_prefix}/lib/%{base_Name}/Plugins/NetRocks/plug
%dir %{_prefix}/lib/%{base_Name}/Plugins/SimpleIndent
%dir %{_prefix}/lib/%{base_Name}/Plugins/tmppanel

%{_prefix}/lib/%{base_Name}/Plugins/align/*
%{_prefix}/lib/%{base_Name}/Plugins/autowrap/*
%{_prefix}/lib/%{base_Name}/Plugins/calc/*
%{_prefix}/lib/%{base_Name}/Plugins/colorer/*
%{_prefix}/lib/%{base_Name}/Plugins/compare/*
%{_prefix}/lib/%{base_Name}/Plugins/drawline/*
%{_prefix}/lib/%{base_Name}/Plugins/editcase/*
%{_prefix}/lib/%{base_Name}/Plugins/editorcomp/*
%{_prefix}/lib/%{base_Name}/Plugins/filecase/*
%{_prefix}/lib/%{base_Name}/Plugins/incsrch/*
%{_prefix}/lib/%{base_Name}/Plugins/inside/*
%{_prefix}/lib/%{base_Name}/Plugins/multiarc/*
%{_prefix}/lib/%{base_Name}/Plugins/SimpleIndent/*
%{_prefix}/lib/%{base_Name}/Plugins/tmppanel/*
%{_prefix}/lib/%{base_Name}/Plugins/NetRocks/plug/NetRocks.far-plug-wide
%{_prefix}/lib/%{base_Name}/Plugins/NetRocks/plug/NetRocks-FILE.broker

%files plugins-ttyxi
%{_prefix}/lib/%{base_Name}/%{base_Name}_ttyx.broker

%files wxgtk
%{_prefix}/lib/%{base_Name}/%{base_Name}_gui.so

%files plugins-netrocks-ftp
%{_prefix}/lib/%{base_Name}/Plugins/NetRocks/plug/NetRocks-FTP.broker

%files plugins-netrocks-sftp
%{_prefix}/lib/%{base_Name}/Plugins/NetRocks/plug/NetRocks-SFTP.broker

%files plugins-netrocks-smb
%{_prefix}/lib/%{base_Name}/Plugins/NetRocks/plug/NetRocks-SMB.broker

%files plugins-netrocks-webdav
%{_prefix}/lib/%{base_Name}/Plugins/NetRocks/plug/NetRocks-WebDAV.broker

%files plugins-netrocks-nfs
%{_prefix}/lib/%{base_Name}/Plugins/NetRocks/plug/NetRocks-NFS.broker

%files plugins-netrocks-shell
%{_prefix}/lib/%{base_Name}/Plugins/NetRocks/plug/NetRocks-SHELL.broker
%dir %_datadir/%{base_Name}/Plugins/NetRocks/plug/SHELL
%_datadir/%{base_Name}/Plugins/NetRocks/plug/SHELL/*

# full files

%if %{use_metapackages}

%files server
%files desktop

%else

%files full
%license LICENSE.txt

%_bindir/%{base_Name}
%_bindir/far2ledit
%dir %{_prefix}/lib/%{base_Name}
%{_prefix}/lib/%{base_Name}/%{base_Name}_askpass
%{_prefix}/lib/%{base_Name}/%{base_Name}_sudoapp
%{_prefix}/lib/%{base_Name}/%{base_Name}_gui.so
%{_prefix}/lib/%{base_Name}/%{base_Name}_ttyx.broker

%dir %_datadir/%{base_Name}
%dir %_datadir/%{base_Name}/Plugins
%dir %_datadir/%{base_Name}/Plugins/align
%dir %_datadir/%{base_Name}/Plugins/autowrap
%dir %_datadir/%{base_Name}/Plugins/calc
%dir %_datadir/%{base_Name}/Plugins/colorer
%dir %_datadir/%{base_Name}/Plugins/compare
%dir %_datadir/%{base_Name}/Plugins/drawline
%dir %_datadir/%{base_Name}/Plugins/editcase
%dir %_datadir/%{base_Name}/Plugins/editorcomp
%dir %_datadir/%{base_Name}/Plugins/filecase
%dir %_datadir/%{base_Name}/Plugins/incsrch
%dir %_datadir/%{base_Name}/Plugins/inside
%dir %_datadir/%{base_Name}/Plugins/multiarc
%dir %_datadir/%{base_Name}/Plugins/SimpleIndent
%dir %_datadir/%{base_Name}/Plugins/NetRocks
%dir %_datadir/%{base_Name}/Plugins/tmppanel
%_datadir/%{base_Name}/*.*

%_datadir/%{base_Name}/Plugins/align/*
%_datadir/%{base_Name}/Plugins/autowrap/*
%_datadir/%{base_Name}/Plugins/calc/*
%_datadir/%{base_Name}/Plugins/colorer/*
%_datadir/%{base_Name}/Plugins/compare/*
%_datadir/%{base_Name}/Plugins/drawline/*
%_datadir/%{base_Name}/Plugins/editcase/*
%_datadir/%{base_Name}/Plugins/editorcomp/*
%_datadir/%{base_Name}/Plugins/filecase/*
%_datadir/%{base_Name}/Plugins/incsrch/*
%_datadir/%{base_Name}/Plugins/inside/*
%_datadir/%{base_Name}/Plugins/multiarc/*
%_datadir/%{base_Name}/Plugins/SimpleIndent/*
%_datadir/%{base_Name}/Plugins/NetRocks/*
%_datadir/%{base_Name}/Plugins/tmppanel/*

%dir %{_prefix}/lib/%{base_Name}/Plugins
%dir %{_prefix}/lib/%{base_Name}/Plugins/align
%dir %{_prefix}/lib/%{base_Name}/Plugins/autowrap
%dir %{_prefix}/lib/%{base_Name}/Plugins/calc
%dir %{_prefix}/lib/%{base_Name}/Plugins/colorer
%dir %{_prefix}/lib/%{base_Name}/Plugins/compare
%dir %{_prefix}/lib/%{base_Name}/Plugins/drawline
%dir %{_prefix}/lib/%{base_Name}/Plugins/editcase
%dir %{_prefix}/lib/%{base_Name}/Plugins/editorcomp
%dir %{_prefix}/lib/%{base_Name}/Plugins/filecase
%dir %{_prefix}/lib/%{base_Name}/Plugins/incsrch
%dir %{_prefix}/lib/%{base_Name}/Plugins/inside
%dir %{_prefix}/lib/%{base_Name}/Plugins/multiarc
%dir %{_prefix}/lib/%{base_Name}/Plugins/NetRocks
%dir %{_prefix}/lib/%{base_Name}/Plugins/SimpleIndent
%dir %{_prefix}/lib/%{base_Name}/Plugins/tmppanel

%{_prefix}/lib/%{base_Name}/Plugins/align/*
%{_prefix}/lib/%{base_Name}/Plugins/autowrap/*
%{_prefix}/lib/%{base_Name}/Plugins/calc/*
%{_prefix}/lib/%{base_Name}/Plugins/colorer/*
%{_prefix}/lib/%{base_Name}/Plugins/compare/*
%{_prefix}/lib/%{base_Name}/Plugins/drawline/*
%{_prefix}/lib/%{base_Name}/Plugins/editcase/*
%{_prefix}/lib/%{base_Name}/Plugins/editorcomp/*
%{_prefix}/lib/%{base_Name}/Plugins/filecase/*
%{_prefix}/lib/%{base_Name}/Plugins/incsrch/*
%{_prefix}/lib/%{base_Name}/Plugins/inside/*
%{_prefix}/lib/%{base_Name}/Plugins/multiarc/*
%{_prefix}/lib/%{base_Name}/Plugins/SimpleIndent/*
%{_prefix}/lib/%{base_Name}/Plugins/tmppanel/*
%{_prefix}/lib/%{base_Name}/Plugins/NetRocks/*

%_datadir/applications/%{base_Name}.desktop

%_datadir/icons/%{base_Name}.svg

# Leap 15.x has no 1024x1024 icon folders
%if ( 0%{?sle_version} <= 150600 ) && 0%{?is_opensuse}
%dir %_datadir/icons/hicolor/1024x1024
%dir %_datadir/icons/hicolor/1024x1024/apps
%endif
%_datadir/icons/hicolor/1024x1024/apps/%{base_Name}.svg
%_datadir/icons/hicolor/128x128/apps/%{base_Name}.svg
%_datadir/icons/hicolor/16x16/apps/%{base_Name}.svg
%_datadir/icons/hicolor/192x192/apps/%{base_Name}.svg
%_datadir/icons/hicolor/24x24/apps/%{base_Name}.svg
%_datadir/icons/hicolor/256x256/apps/%{base_Name}.svg
%_datadir/icons/hicolor/32x32/apps/%{base_Name}.svg
%_datadir/icons/hicolor/48x48/apps/%{base_Name}.svg
%_datadir/icons/hicolor/512x512/apps/%{base_Name}.svg
%_datadir/icons/hicolor/64x64/apps/%{base_Name}.svg
%_datadir/icons/hicolor/72x72/apps/%{base_Name}.svg
%_datadir/icons/hicolor/96x96/apps/%{base_Name}.svg

%if %{has_copyright}
%{_prefix}/share/doc/far2l/copyright
%endif

%{_prefix}/share/man/man1/*
%{_prefix}/share/man/ru/man1/*

%endif

# 3rd party plugins
%if %{use_netcfg}
%files plugins-netcfg
%dir %_datadir/%{base_Name}/Plugins/netcfg
%dir %{_prefix}/lib/%{base_Name}/Plugins/netcfg
%_datadir/%{base_Name}/Plugins/netcfg/*
%{_prefix}/lib/%{base_Name}/Plugins/netcfg/*
%endif

%if %{use_sqlitep}
%files plugins-sqlite
%dir %_datadir/%{base_Name}/Plugins/sqlplugin
%dir %{_prefix}/lib/%{base_Name}/Plugins/sqlplugin
%_datadir/%{base_Name}/Plugins/sqlplugin/*
%{_prefix}/lib/%{base_Name}/Plugins/sqlplugin/*
%endif

%if %{use_procp}
%files plugins-processes
%dir %_datadir/%{base_Name}/Plugins/processes
%dir %{_prefix}/lib/%{base_Name}/Plugins/processes
%_datadir/%{base_Name}/Plugins/processes/*
%{_prefix}/lib/%{base_Name}/Plugins/processes/*
%endif

%if %{use_jumpword}
%files plugins-jumpword
%dir %_datadir/%{base_Name}/Plugins/jumpword
%dir %{_prefix}/lib/%{base_Name}/Plugins/jumpword
%_datadir/%{base_Name}/Plugins/jumpword/*
%{_prefix}/lib/%{base_Name}/Plugins/jumpword/*
%endif

%if %{use_editwrap}
%files plugins-editwrap
%dir %{_prefix}/lib/%{base_Name}/Plugins/far2l-EditWrap
%{_prefix}/lib/%{base_Name}/Plugins/far2l-EditWrap/*
%endif

# Python
%if %{use_python_plugin}
%files plugins-python
%dir %{_prefix}/lib/%{base_Name}/Plugins/python
%dir %_datadir/%{base_Name}/Plugins/python

%{_prefix}/lib/%{base_Name}/Plugins/python/*
%_datadir/%{base_Name}/Plugins/python/*
%endif

# Lua
%if %{use_lua_plugin}
%files plugins-lua
%dir %{_prefix}/lib/%{base_Name}/Plugins/luafar
%dir %_datadir/%{base_Name}/Plugins/luafar

%{_prefix}/lib/%{base_Name}/Plugins/luafar/*
%_datadir/%{base_Name}/Plugins/luafar/*
%endif

%if 0%{?sle_version} > 150600 && 0%{?is_opensuse}
%if %{use_metapackages}
%else
%changelog full
%include %{SOURCE1}
%endif

%changelog core
%include %{SOURCE1}
%endif
