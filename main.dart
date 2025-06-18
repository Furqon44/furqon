import 'package:flutter/material.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:intl/intl.dart';
import 'dart:async';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(
    options: const FirebaseOptions(
      apiKey: "AIzaSyB4_8od06WpwWi0JnH5AsumeMfS4J-6z94",
      authDomain: "tess-2d281.firebaseapp.com",
      databaseURL: "https://tess-2d281-default-rtdb.firebaseio.com",
      projectId: "tess-2d281",
      storageBucket: "tess-2d281.firebasestorage.app",
      messagingSenderId: "125497189118",
      appId: "1:125497189118:web:c5e86434ed027f1b8fbbaf",
      measurementId: "G-35VV44S694",
    ),
  );
  
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Smart Planting Pot',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        primarySwatch: Colors.green,
        scaffoldBackgroundColor: Colors.white,
      ),
      home: const HomeScreen(),
    );
  }
}

class HomeScreen extends StatefulWidget {
  const HomeScreen({Key? key}) : super(key: key);

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  final DatabaseReference _database = FirebaseDatabase.instance.ref();
  
  String _humidity = "50%";
  String _temperature = "50%";
  String _soilMoisture = "50%";
  String _light = "50%";
  String _currentTime = "00:00";
  String _greeting = "Good Morning";
  
  bool _isWaterPumpOn = false;
  bool _isFertilizerPumpOn = false;
  
  String _waterPumpRange = "50%-80%";
  String _fertilizerPumpDate = "30-03-2025";
  String _fertilizerPumpTime = "12:00";
  
  final TextEditingController _waterPumpRangeController = TextEditingController();
  final TextEditingController _fertilizerPumpDateController = TextEditingController();
  final TextEditingController _fertilizerPumpTimeController = TextEditingController();

  @override
  void initState() {
    super.initState();
    _setupFirebaseListeners();
    _setupCurrentTime();
  }

  void _setupCurrentTime() {
    Timer.periodic(Duration(seconds: 1), (timer) {
      final now = DateTime.now();
      setState(() {
        _currentTime = DateFormat('HH:mm:ss').format(now);
        _updateGreeting(now.hour);
      });
    });
  }
  
  void _updateGreeting(int hour) {
    if (hour >= 5 && hour < 12) {
      _greeting = "Good Morning";
    } else if (hour >= 12 && hour < 17) {
      _greeting = "Good Afternoon";
    } else if (hour >= 17 && hour < 21) {
      _greeting = "Good Evening";
    } else {
      _greeting = "Good Night";
    }
  }

  void _setupFirebaseListeners() {
    _database.child('sensors').onValue.listen((event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>?;
      if (data != null) {
        setState(() {
          _humidity = "${data['humidity'] ?? 50}%";
          _temperature = "${(data['temperature'] ?? 25).toStringAsFixed(1)}°C";
          _soilMoisture = "${data['soilMoisture'] ?? 50}%";
          _light = "${data['light'] ?? 50}%";
        });
      }
    });

    _database.child('pumps').onValue.listen((event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>?;
      if (data != null) {
        setState(() {
          _isWaterPumpOn = data['waterPump'] ?? false;
          _isFertilizerPumpOn = data['fertilizerPump'] ?? false;
        });
      }
    });

    _database.child('pump_settings').onValue.listen((event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>?;
      if (data != null) {
        setState(() {
          _waterPumpRange = data['waterPumpRange'] ?? "50%-80%";
          _fertilizerPumpDate = data['fertilizerPumpDate'] ?? "30-03-2025";
          _fertilizerPumpTime = data['fertilizerPumpTime'] ?? "12:00";
        });
      }
    });
  }

  void _togglePump(String pumpName, bool value) {
    _database.child('pumps').update({pumpName: value});
  }

  void _updatePumpSettings() {
    _database.child('pump_settings').update({
      'waterPumpRange': _waterPumpRangeController.text,
      'fertilizerPumpDate': _fertilizerPumpDateController.text,
      'fertilizerPumpTime': _fertilizerPumpTimeController.text,
    });
    
    setState(() {
      _waterPumpRange = _waterPumpRangeController.text;
      _fertilizerPumpDate = _fertilizerPumpDateController.text;
      _fertilizerPumpTime = _fertilizerPumpTimeController.text;
    });
    
    Navigator.pop(context);
  }

  Future<void> _selectDate(BuildContext context) async {
    final DateTime? picked = await showDatePicker(
      context: context,
      initialDate: DateTime.now(),
      firstDate: DateTime.now(),
      lastDate: DateTime(2030),
    );
    if (picked != null) {
      setState(() {
        _fertilizerPumpDateController.text = DateFormat('dd-MM-yyyy').format(picked);
      });
    }
  }

  Future<void> _selectTime(BuildContext context) async {
    final TimeOfDay? picked = await showTimePicker(
      context: context,
      initialTime: TimeOfDay.now(),
    );
    if (picked != null) {
      setState(() {
        _fertilizerPumpTimeController.text = 
          picked.hour.toString().padLeft(2, '0') + ':' + 
          picked.minute.toString().padLeft(2, '0');
      });
    }
  }

  void _showPumpSettingsDialog() {
    _waterPumpRangeController.text = _waterPumpRange;
    _fertilizerPumpDateController.text = _fertilizerPumpDate;
    _fertilizerPumpTimeController.text = _fertilizerPumpTime;

    showDialog(
      context: context,
      builder: (BuildContext context) {
        return AlertDialog(
          title: const Text('Pump Settings'),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                TextField(
                  controller: _waterPumpRangeController,
                  decoration: const InputDecoration(
                    labelText: 'Water Pump Range',
                    hintText: 'Enter range (e.g., 50%-80%)',
                  ),
                ),
                Row(
                  children: [
                    Expanded(
                      child: TextField(
                        controller: _fertilizerPumpDateController,
                        decoration: const InputDecoration(
                          labelText: 'Fertilizer Pump Date',
                          hintText: 'Select Date',
                        ),
                        readOnly: true,
                        onTap: () => _selectDate(context),
                      ),
                    ),
                    IconButton(
                      icon: const Icon(Icons.calendar_today),
                      onPressed: () => _selectDate(context),
                    ),
                  ],
                ),
                Row(
                  children: [
                    Expanded(
                      child: TextField(
                        controller: _fertilizerPumpTimeController,
                        decoration: const InputDecoration(
                          labelText: 'Fertilizer Pump Time',
                          hintText: 'Select Time',
                        ),
                        readOnly: true,
                        onTap: () => _selectTime(context),
                      ),
                    ),
                    IconButton(
                      icon: const Icon(Icons.access_time),
                      onPressed: () => _selectTime(context),
                    ),
                  ],
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Cancel'),
            ),
            ElevatedButton(
              onPressed: _updatePumpSettings,
              child: const Text('Save'),
            ),
          ],
        );
      },
    );
  }

  void _showInfoDialog() {
    showDialog(
      context: context,
      builder: (BuildContext context) {
        return AlertDialog(
          title: const Text('Settings Information'),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'Water Pump Range:',
                  style: TextStyle(
                    fontWeight: FontWeight.bold,
                    fontSize: 16,
                  ),
                ),
                const SizedBox(height: 8),
                Text(
                  'Current setting: $_waterPumpRange',
                  style: const TextStyle(fontSize: 14),
                ),
                const SizedBox(height: 16),
                const Text(
                  'Fertilizer Schedule:',
                  style: TextStyle(
                    fontWeight: FontWeight.bold,
                    fontSize: 16,
                  ),
                ),
                const SizedBox(height: 8),
                Text(
                  'Scheduled for: $_fertilizerPumpDate at $_fertilizerPumpTime',
                  style: const TextStyle(fontSize: 14),
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Close'),
            ),
          ],
        );
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: SingleChildScrollView(
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text(
                          "HELLO,",
                          style: TextStyle(
                            fontSize: 28,
                            fontWeight: FontWeight.bold,
                            color: Colors.black87,
                          ),
                        ),
                        Text(
                          _greeting,
                          style: const TextStyle(
                            fontSize: 28,
                            fontWeight: FontWeight.bold,
                            color: Colors.black87,
                          ),
                        ),
                      ],
                    ),
                    Row(
                      children: [
                        IconButton(
                          icon: const Icon(Icons.settings_outlined, size: 28),
                          color: Colors.green,
                          onPressed: _showPumpSettingsDialog,
                        ),
                        IconButton(
                          icon: const Icon(Icons.info_outline, size: 28),
                          color: Colors.green,
                          onPressed: _showInfoDialog,
                        ),
                      ],
                    ),
                  ],
                ),
                const SizedBox(height: 24),
                Center(
                  child: Column(
                    children: [
                      Container(
                        width: 120,
                        height: 120,
                        decoration: const BoxDecoration(
                          shape: BoxShape.circle,
                          color: Colors.white,
                          boxShadow: [
                            BoxShadow(
                              color: Colors.black12,
                              blurRadius: 10,
                              offset: Offset(0, 4),
                            )
                          ],
                        ),
                        child: const Center(
                          child: Icon(
                            Icons.energy_savings_leaf,
                            color: Colors.green,
                            size: 80,
                          ),
                        ),
                      ),
                      const SizedBox(height: 12),
                      Container(
                        width: 250,
                        padding: const EdgeInsets.symmetric(
                          horizontal: 16,
                          vertical: 16,
                        ),
                        decoration: BoxDecoration(
                          color: Colors.white,
                          borderRadius: BorderRadius.circular(16),
                          boxShadow: [
                            BoxShadow(
                              color: Colors.black12,
                              blurRadius: 10,
                              offset: Offset(0, 4),
                            )
                          ],
                        ),
                        child: Column(
                          children: [
                            const Text(
                              "Smart Planting pot",
                              style: TextStyle(
                                fontSize: 16,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                            const SizedBox(height: 4),
                            Text(
                              _currentTime,
                              style: const TextStyle(
                                fontSize: 14,
                                color: Colors.grey,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 24),
                GridView.count(
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  crossAxisCount: 2,
                  mainAxisSpacing: 16,
                  crossAxisSpacing: 16,
                  childAspectRatio: 1.2,
                  children: [
                    _buildSensorCard(
                      icon: Icons.water_drop,
                      label: "Humidity",
                      value: _humidity,
                    ),
                    _buildSensorCard(
                      icon: Icons.device_thermostat,
                      label: "Temperature",
                      value: _temperature,
                    ),
                    _buildSensorCard(
                      icon: Icons.opacity,
                      label: "Soil Moisture",
                      value: _soilMoisture,
                    ),
                    _buildSensorCard(
                      icon: Icons.light_mode,
                      label: "Light",
                      value: _light,
                    ),
                  ],
                ),
                const SizedBox(height: 24),
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                  children: [
                    Column(
                      children: [
                        const Text(
                          'Water Pump',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                        const SizedBox(height: 8),
                        Switch(
                          activeColor: Colors.green,
                          value: _isWaterPumpOn,
                          onChanged: (value) => _togglePump('waterPump', value),
                        ),
                      ],
                    ),
                    Column(
                      children: [
                        const Text(
                          'Fertilizing Pump',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                        const SizedBox(height: 8),
                        Switch(
                          activeColor: Colors.green,
                          value: _isFertilizerPumpOn,
                          onChanged: (value) => _togglePump('fertilizerPump', value),
                        ),
                      ],
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildSensorCard({
    required IconData icon,
    required String label,
    required String value,
  }) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(16),
        boxShadow: [
          BoxShadow(
            color: Colors.black12,
            blurRadius: 10,
            offset: Offset(0, 4),
          )
        ],
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Icon(icon, size: 32, color: Colors.green),
          const SizedBox(height: 8),
          Text(
            label,
            style: const TextStyle(
              fontWeight: FontWeight.w600,
              fontSize: 14,
            ),
            textAlign: TextAlign.center,
          ),
          const SizedBox(height: 4),
          Text(
            value,
            style: const TextStyle(
              fontSize: 14,
              color: Colors.grey,
            ),
            textAlign: TextAlign.center,
          ),
        ],
      ),
    );
  }
}