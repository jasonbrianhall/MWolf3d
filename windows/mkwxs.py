#!/usr/bin/env python3
"""
mkwxs.py RELEASEDIR OUT.wxs VERSION [--no-ui]

The WiX 3 source of the installer, from the release directory as
`make release` leaves it (WL6\\, WL6APO\\, WL1\\, SOD\\, SDM\\: the game, SETUP,
IPXSETUP, the co-op and MIDI files, MUSIC\\; README.TXT and wolf3d.ico):

  candle -arch x64 wolf3d.wxs && light -ext WixUIExtension wolf3d.wixobj

(--no-ui: no dialogs, and only what msitools' wixl knows, to try it out.)

It installs for the user who runs it (no administrator), in
%LOCALAPPDATA%\\Programs\\Wolfenstein 3-D: the game's data files, its
CONFIG and saved games go next to each game, so the folders have to be
the user's to write in. Uninstalling leaves what the user put there.
Start menu: Wolfenstein 3-D, Spear of Destiny, their SETUPs, the folder
and the README; a desktop shortcut too, unless DESKTOP_SHORTCUT=0.
"""

import os
import sys
import uuid
import hashlib
from xml.sax.saxutils import quoteattr

UPGRADE_CODE = "5318EAF4-E73B-4E8F-BC11-A889A7B48615"
NAMESPACE = uuid.UUID("BA6ED6EE-137F-480A-A74A-13F554C3CC65")	# component GUIDs
REGKEY = r"Software\Wolfenstein 3-D"

# Start menu: (folder, program, name, description)
SHORTCUTS = [
	("WL6", "wolf3d.exe", "Wolfenstein 3-D", "Wolfenstein 3-D (put the .WL6 data files in its folder)"),
	("WL6", "setup.exe", "Wolfenstein 3-D Setup", "Sound, keys, mouse, display and network games"),
	("SOD", "spear.exe", "Spear of Destiny", "Spear of Destiny (put the .SOD data files in its folder)"),
	("SOD", "setup.exe", "Spear of Destiny Setup", "Sound, keys, mouse, display and network games"),
]


def ident(prefix, path):
	"""A WiX Id for a path: letters, digits, _ and ., at most 72"""
	h = hashlib.sha1(path.replace("\\", "/").lower().encode()).hexdigest()[:16]
	base = "".join(c if c.isalnum() else "_" for c in os.path.basename(path))[:40]
	return "%s_%s_%s" % (prefix, base, h)


def guid(path):
	return str(uuid.uuid5(NAMESPACE, path.replace("\\", "/").lower())).upper()


def main():
	if len(sys.argv) < 4:
		sys.exit(__doc__)
	root, out, version = sys.argv[1], sys.argv[2], sys.argv[3]
	ui = "--no-ui" not in sys.argv

	lines = []
	w = lines.append
	components = []

	def directory(rel, depth):
		"""<Directory> for root/rel, its files in one component, and the folders in it"""
		full = os.path.join(root, rel) if rel else root
		ind = "\t" * depth
		entries = sorted(os.listdir(full))
		files = [e for e in entries if os.path.isfile(os.path.join(full, e))]
		dirs = [e for e in entries if os.path.isdir(os.path.join(full, e))]
		dirid = "INSTALLFOLDER" if not rel else ident("D", rel)
		if rel:
			w('%s<Directory Id="%s" Name=%s>' % (ind, dirid, quoteattr(os.path.basename(rel))))
		compid = ident("C", rel or "root")
		components.append(compid)
		w('%s\t<Component Id="%s" Guid="%s" Win64="yes">' % (ind, compid, guid("dir:" + (rel or "."))))
		# a per-user component's key is in HKCU; the folder goes when it's empty
		w('%s\t\t<RegistryValue Root="HKCU" Key="%s\\Files" Name=%s Type="integer" Value="1" KeyPath="yes" />'
			% (ind, REGKEY, quoteattr(rel or ".")))
		w('%s\t\t<RemoveFolder Id="%s" On="uninstall" />' % (ind, ident("R", rel or "root")))
		if not rel:		# (%LOCALAPPDATA%\Programs too, if it's left empty: ICE64)
			w('%s\t\t<RemoveFolder Id="RUserPrograms" Directory="UserPrograms" On="uninstall" />' % ind)
		for f in files:
			path = os.path.join(rel, f) if rel else f
			w('%s\t\t<File Id="%s" Name=%s Source=%s />'
				% (ind, ident("F", path), quoteattr(f), quoteattr(path.replace("/", "\\") if ui else path)))
		w("%s\t</Component>" % ind)
		for d in dirs:
			directory(os.path.join(rel, d) if rel else d, depth + 1)
		if rel:
			w("%s</Directory>" % ind)

	w('<?xml version="1.0" encoding="UTF-8"?>')
	w('<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">')
	w('\t<Product Id="*" Name="Wolfenstein 3-D" Language="1033" Version="%s"' % version)
	w('\t         Manufacturer="jasonbrianhall/wolf3d" UpgradeCode="%s">' % UPGRADE_CODE)
	w('\t\t<Package InstallerVersion="500" Compressed="yes" InstallScope="perUser"%s'
		% (' InstallPrivileges="limited"' if ui else ''))
	w('\t\t         Description="Wolfenstein 3-D and Spear of Destiny (id Software\'s source, for Windows)"')
	w('\t\t         Manufacturer="jasonbrianhall/wolf3d" />')
	w('\t\t<MajorUpgrade DowngradeErrorMessage="A newer version of Wolfenstein 3-D is already installed." />')
	w('\t\t<MediaTemplate EmbedCab="yes"%s />' % (' CompressionLevel="high"' if ui else ''))
	w('\t\t<Icon Id="WolfIcon" SourceFile="wolf3d.ico" />')
	w('\t\t<Property Id="ARPPRODUCTICON" Value="WolfIcon" />')
	w('\t\t<Property Id="ARPURLINFOABOUT" Value="https://github.com/jasonbrianhall/wolf3d" />')
	w('\t\t<Property Id="ARPHELPLINK" Value="https://github.com/jasonbrianhall/wolf3d" />')
	w('\t\t<Property Id="ARPCOMMENTS" Value="Copy your game\'s data files into the folder of the game they\'re for (WL6, SOD...)" />')
	w('\t\t<Property Id="DESKTOP_SHORTCUT" Value="1" />')
	if ui:
		w('\t\t<WixVariable Id="WixUILicenseRtf" Value="License.rtf" />')
		w('\t\t<Property Id="WIXUI_INSTALLDIR" Value="INSTALLFOLDER" />')

	w('\t\t<Directory Id="TARGETDIR" Name="SourceDir">')
	w('\t\t\t<Directory Id="LocalAppDataFolder">')
	w('\t\t\t\t<Directory Id="UserPrograms" Name="Programs">')
	w('\t\t\t\t\t<Directory Id="INSTALLFOLDER" Name="Wolfenstein 3-D">')
	directory("", 6)
	w('\t\t\t\t\t</Directory>')
	w('\t\t\t\t</Directory>')
	w('\t\t\t</Directory>')
	w('\t\t\t<Directory Id="ProgramMenuFolder">')
	w('\t\t\t\t<Directory Id="ApplicationProgramsFolder" Name="Wolfenstein 3-D" />')
	w('\t\t\t</Directory>')
	w('\t\t\t<Directory Id="DesktopFolder" />')
	w('\t\t</Directory>')

	# the Start menu
	w('\t\t<DirectoryRef Id="ApplicationProgramsFolder">')
	w('\t\t\t<Component Id="StartMenu" Guid="%s" Win64="yes">' % guid("startmenu"))
	for ed, exe, name, desc in SHORTCUTS:
		edir = ident("D", ed)
		w('\t\t\t\t<Shortcut Id="%s" Name=%s Description=%s Target="[%s]%s" WorkingDirectory="%s" Icon="WolfIcon" />'
			% (ident("S", ed + exe), quoteattr(name), quoteattr(desc), edir, exe, edir))
	w('\t\t\t\t<Shortcut Id="SFolder" Name="Game folders" Description="Where the data files go: one folder for each game"'
		' Target="[INSTALLFOLDER]" />')
	w('\t\t\t\t<Shortcut Id="SReadme" Name="Read me" Description="How to set the game up" Target="[INSTALLFOLDER]README.TXT" />')
	w('\t\t\t\t<RemoveFolder Id="RStartMenu" On="uninstall" />')
	w('\t\t\t\t<RegistryValue Root="HKCU" Key="%s\\Shortcuts" Name="StartMenu" Type="integer" Value="1" KeyPath="yes" />' % REGKEY)
	w('\t\t\t</Component>')
	w('\t\t</DirectoryRef>')

	w('\t\t<DirectoryRef Id="DesktopFolder">')
	w('\t\t\t<Component Id="DesktopShortcut" Guid="%s" Win64="yes">' % guid("desktop"))
	if ui:
		w('\t\t\t\t<Condition>DESKTOP_SHORTCUT=1</Condition>')
	edir = ident("D", "WL6")
	w('\t\t\t\t<Shortcut Id="SDesktop" Name="Wolfenstein 3-D" Description="Wolfenstein 3-D"'
		' Target="[%s]wolf3d.exe" WorkingDirectory="%s" Icon="WolfIcon" />' % (edir, edir))
	w('\t\t\t\t<RegistryValue Root="HKCU" Key="%s\\Shortcuts" Name="Desktop" Type="integer" Value="1" KeyPath="yes" />' % REGKEY)
	w('\t\t\t</Component>')
	w('\t\t</DirectoryRef>')

	w('\t\t<Feature Id="MainFeature" Title="Wolfenstein 3-D" Level="1">')
	for c in components + ["StartMenu", "DesktopShortcut"]:
		w('\t\t\t<ComponentRef Id="%s" />' % c)
	w('\t\t</Feature>')
	if ui:
		w('\t\t<UIRef Id="WixUI_InstallDir" />')
		w('\t\t<UIRef Id="WixUI_ErrorProgressText" />')
	w('\t</Product>')
	w('</Wix>')

	with open(out, "w", encoding="utf-8", newline="\r\n") as f:
		f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
	main()
