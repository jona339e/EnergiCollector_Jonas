let socket;
let chart;
let dataPoints = [];

document.addEventListener("DOMContentLoaded", function () {
  // Initialize WebSocket
  socket = new WebSocket(`ws://${window.location.hostname}/ws`);
  
  socket.onopen = function () {
    console.log("WebSocket connection established");
    // Request the whole log when the WebSocket connection is established
    socket.send(JSON.stringify({ request: "wholeLog" }));
  };

  socket.onmessage = function (event) {
    let data = JSON.parse(event.data);
    console.log("Data received: ", data);

    if (Array.isArray(data)) {
      // If data is an array, it is the whole log
      dataPoints = data.map(entry => {
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
