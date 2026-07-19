struct BackendOptions {
	bool UseModernLook;     	// unicode glyphs for check boxes / radio buttons
	bool UseRoundedBorders; 	// wx only: rounded corners for borders
	bool UseSingleBordersOnly;	// wx only: replace double borders to single
	bool UseNoBorders;			// wx only: replace double borders to single
	bool UseEmbossAsBold;		// wx only: repplace bold glyphs to emboss effect
	bool UseSoftenBevels;		// wx only: makes boxes less bright for black and white case
	bool Use3D;                 // wx only: 3d buttons
};
