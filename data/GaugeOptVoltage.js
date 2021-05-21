var voltageOpts = {
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
		labels: [200, 210, 220, 230, 240],  // Print labels at these values
		color: "#aaaaee",  // Optional: Label text color
		fractionDigits: 0  // Optional: Numerical precision. 0=round off.
	  },
	staticZones: [					
		{strokeStyle: "#FFDD00", min: 200, max: 220}, // Yellow
		{strokeStyle: "#30B32D", min: 220, max: 230}, // Green
		{strokeStyle: "#F03E3E", min: 230, max: 240}  // Red
	],
	limitMax: true,     // If false, max value increases automatically if value > maxValue
	limitMin: true,     // If true, the min value of the gauge will be fixed
	colorStart: '#6FADCF',   // Colors
	colorStop: '#8FC0DA',    // just experiment with them
	strokeColor: '#E0E0E0',  // to see which ones work best for you
	generateGradient: true,
	highDpiSupport: true,     // High resolution support
};