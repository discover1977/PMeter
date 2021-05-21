var pfOpts = {
	angle: 0.0, // The span of the gauge arc
	lineWidth: 0.2, // The line thickness
	radiusScale: 1.0, // Relative radius
	pointer: {
		length: 0.4, // // Relative to gauge radius
		strokeWidth: 0.042, // The thickness
		color: '#0f0f0f' // Fill color
	},
	renderTicks: {
		divisions: 4,
		divWidth: 1.1,
		divLength: 0.7,
		divColor: '#808080',
		subDivisions: 5,
		subLength: 0.5,
		subWidth: 0.6,
		subColor: '#aaaaaa'
	},
	staticLabels: {
		font: "8px sans-serif",  // Specifies font
		labels: [0, 1],  // Print labels at these values
		color: "#aaaaee",  // Optional: Label text color
		fractionDigits: 0  // Optional: Numerical precision. 0=round off.
	},
	staticZones: [					
		{strokeStyle: "#8B0000", min: 0.0, max: 0.5},
		{strokeStyle: "#DC143C", min: 0.5, max: 0.65},
		{strokeStyle: "#FFA500", min: 0.65, max: 0.8},  
		{strokeStyle: "#ADFF2F", min: 0.8, max: 0.85},
		{strokeStyle: "#00FF00", min: 0.85, max: 1.0}
	],
	limitMax: true,     // If false, max value increases automatically if value > maxValue
	limitMin: true,     // If true, the min value of the gauge will be fixed
	colorStart: '#6FADCF',   // Colors
	colorStop: '#8FC0DA',    // just experiment with them
	strokeColor: '#E0E0E0',  // to see which ones work best for you
	generateGradient: true,
	highDpiSupport: true,     // High resolution support
};