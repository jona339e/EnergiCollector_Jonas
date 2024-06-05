let socket;
let chart;
let dataPoints = [];

function deleteDataLogFile() {
  socket.send(JSON.stringify({ request: "deleteDataLogFile" }));
  // empty datapoints
  dataPoints = [];
  updateChart();
}

document.addEventListener("DOMContentLoaded", function () {
  // Initialize WebSocket
  socket = new WebSocket(`ws://${window.location.hostname}/ws`);
  
  socket.onopen = function () {
    console.log("WebSocket connection established");
    // Request the whole log when the WebSocket connection is established
    socket.send(JSON.stringify({ request: "wholeLog" }));
  };

  // Delete button click event handler
  document.getElementById("deleteBtn").addEventListener("click", function() {
    deleteDataLogFile();
  });

  socket.onmessage = function (event) {
    let data = JSON.parse(event.data);
    console.log("Data received: ", data);

    if (data.log && Array.isArray(data.log)) {
      // If data.log is an array, it is the whole log
      dataPoints = data.log.map(entry => {
        return {
          x: entry.time * 1000,
          y: entry.accumulatedValue
        };
      });
    } else {
      // If data is an object, it is a single log entry
      dataPoints.push({
        x: data.time * 1000,
        y: data.accumulatedValue
      });
    }

    // Update the chart
    updateChart();
  };

  socket.onclose = function () {
    console.log("WebSocket connection closed");
  };

  // Initialize the chart
  chart = Highcharts.chart('container', {
    chart: {
      type: 'line'
    },
    title: {
      text: 'Energy Collector Data'
    },
    xAxis: {
      type: 'datetime',
      title: {
        text: 'Time'
      }
    },
    yAxis: {
      title: {
        text: 'Accumulated Value'
      }
    },
    series: [{
      name: 'Impulse Data',
      data: dataPoints
    }]
  });
});

function updateChart() {
  chart.series[0].setData(dataPoints);
}
