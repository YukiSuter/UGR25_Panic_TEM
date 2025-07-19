// Temps

var logging = false;
let retryCount = 0;
const maxRetries = 3;
let currentIPIndex = 0;

start_logging_button = document.getElementById("monitor_button");
ip_textbox = document.getElementById("ips"); // Add this textbox to your HTML

start_logging_button.addEventListener('click', () => {
    if (logging) {
        logging = false;
        start_logging_button.value = "🔴 Start Logging"
    } else {
        logging = true;
        start_logging_button.value = "🟢 Stop Logging"
        currentIPIndex = 0; // Reset to first IP when starting
        setTimeout(update, 0);
    }
});

// Get list of IPs from textbox
function getIPList() {
    const ipText = ip_textbox.value.trim();
    if (!ipText) {
        console.error('No IP addresses entered in textbox');
        return [];
    }
    
    // Split by comma and clean up whitespace
    const ips = ipText.split(',').map(ip => ip.trim()).filter(ip => ip.length > 0);
    return ips;
}

// Get current IP to poll
function getCurrentIP() {
    const ips = getIPList();
    if (ips.length === 0) return null;
    
    return ips[currentIPIndex % ips.length];
}

// Move to next IP in the list
function moveToNextIP() {
    const ips = getIPList();
    if (ips.length > 1) {
        currentIPIndex = (currentIPIndex + 1) % ips.length;
        console.log(`Switching to next IP: ${getCurrentIP()}`);
    }
}

// Fetch thermistor data from ESP32
async function pollThermistors() {
    const currentIP = getCurrentIP();
    if (!currentIP) {
        console.error('No valid IP address available');
        return null;
    }
    
    try {
        console.log(`Fetching thermistor data from ${currentIP}...`);
        const response = await fetch(`http://${currentIP}/api/thermistors`);
        
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        const tableBody = document.getElementById('table_body');
    
        // Clear existing rows
        tableBody.innerHTML = '';
    
        
        const data = await response.json();
        
        // Log to console
        console.log(`=== Thermistor Data from ${currentIP} (${data.count} thermistors) ===`);
        console.log(`ESP32 Timestamp: ${data.timestamp}`);
        console.log(`Local Time: ${new Date().toLocaleTimeString()}`);
        console.log(data);
        
        data.thermistors.forEach((therm, index) => {
            if (therm != null) {
                console.log(`[${index}] Segment: ${therm.segmentNumber}, Thermistor: ${therm.thermistorNumber}, Temp: ${therm.temperature}°C`);
                const row = document.createElement('tr');
                row.className = "border-b border-zinc-600 hover:bg-zinc-650";
            
                // Create cells for each data point
                
                const segmentCell = document.createElement('td');
                segmentCell.textContent = therm.segmentNumber;
                segmentCell.className = "text-white px-3 py-1";
                
                const thermistorCell = document.createElement('td');
                thermistorCell.textContent = therm.thermistorNumber;
                thermistorCell.className = "text-white px-3 py-1";
                
                const temperatureCell = document.createElement('td');
                temperatureCell.textContent = `${therm.temperature}°C`;
                temperatureCell.className = "text-white px-3 py-1";
                
                // Append cells to row
                row.appendChild(segmentCell);
                row.appendChild(thermistorCell);
                row.appendChild(temperatureCell);

                tableBody.appendChild(row);
            }
            
        });

        
        
        return data;
        
    } catch (error) {
        console.error(`Failed to fetch thermistor data from ${currentIP}: ${error.message}`);
        retryCount++;
        
        if (retryCount >= maxRetries) {
            console.error(`Max retries (${maxRetries}) reached for ${currentIP}. Moving to next IP.`);
            moveToNextIP();
            retryCount = 0; // Reset retry count for next IP
            
            // If we've tried all IPs and still failing, stop logging
            const ips = getIPList();
            if (ips.length === 1) {
                console.error('Only one IP available and it failed. Stopping logging.');
                logging = false;
                start_logging_button.value = "🔴 Start Logging";
                return null;
            }
        }
        
        console.log(`Retry ${retryCount}/${maxRetries} for ${currentIP} on next poll cycle...`);
        return null;
    }
}

// Main update function that handles the polling loop
async function update() {
    if (!logging) {
        console.log('Logging stopped');
        return;
    }
    
    // Check if we have valid IPs
    const ips = getIPList();
    if (ips.length === 0) {
        console.error('No IP addresses configured. Please enter IP addresses in the textbox.');
        logging = false;
        start_logging_button.value = "🔴 Start Logging";
        return;
    }
    
    // Poll the thermistors
    const data = await pollThermistors();
    
    // Continue polling if logging is still active
    if (logging) {
        setTimeout(update, 2000); // Poll every 2 seconds
    }
}

// Optional: Function to poll once without starting continuous logging
async function pollOnce() {
    console.log('Polling thermistors once...');
    currentIPIndex = 0; // Reset to first IP
    await pollThermistors();
}

// Optional: Function to clear console (if you want to add a button for this)
function clearConsole() {
    console.clear();
    console.log('Console cleared - Thermistor Monitor Ready');
}

// Optional: Function to test all IPs once
async function testAllIPs() {
    const ips = getIPList();
    if (ips.length === 0) {
        console.error('No IP addresses configured');
        return;
    }
    
    console.log(`Testing ${ips.length} IP addresses...`);
    
    for (let i = 0; i < ips.length; i++) {
        currentIPIndex = i;
        console.log(`\nTesting IP ${i + 1}/${ips.length}: ${getCurrentIP()}`);
        await pollThermistors();
        
        // Add a small delay between tests
        if (i < ips.length - 1) {
            await new Promise(resolve => setTimeout(resolve, 1000));
        }
    }
    
    currentIPIndex = 0; // Reset to first IP
    console.log('\nIP testing complete');
}