"use client";
import { useEffect, useState, useRef } from "react";
import mqtt from "mqtt";
import AddCard from "../components/AddCard";
import TagCard from "../components/TagCard";

export default function Home() {
  const [tags, setTags] = useState([]);
  const [adding, setAdding] = useState(false);
  const [deletingUid, setDeletingUid] = useState(null);
  const [mqttConnected, setMqttConnected] = useState(false);
  const clientRef = useRef(null);

  // MQTT Broker & Topics
  const BROKER_URL = "wss://41266a43e35b45f8ae3fd8767055a173.s1.eu.hivemq.cloud:8884/mqtt";
  const BROKER_USERNAME = "smart_lock_web";
  const BROKER_PASSWORD = "Smart@pass2025";
  
  const TOPIC_CMD = "smartlock/esp32/cmd";
  const TOPIC_EVENTS = "smartlock/esp32/events";
  const TOPIC_TAGS = "smartlock/esp32/tags";

  useEffect(() => {
    const client = mqtt.connect(BROKER_URL, {
      username: BROKER_USERNAME,
      password: BROKER_PASSWORD,
      clientId: "web-client-" + Math.random().toString(16).substring(2, 8),
      reconnectPeriod: 3000,
    });
    clientRef.current = client;

    client.on("connect", () => {
      console.log("MQTT connected");
      setMqttConnected(true);
      client.subscribe(TOPIC_EVENTS);
      client.subscribe(TOPIC_TAGS);
      client.publish(TOPIC_CMD, "fetchTags");
    });

    client.on("close", () => {
      setMqttConnected(false);
    });

    client.on("message", (topic, message) => {
      const msg = message.toString();
      if (topic === TOPIC_TAGS) {
        try {
          const parsed = JSON.parse(msg);
          setTags(parsed.tags || []);
        } catch (e) {
          console.warn("Failed parse tags:", msg);
        }
      } else if (topic === TOPIC_EVENTS) {
        if (msg.startsWith("tagAdded:")) {
          setAdding(false);
          client.publish(TOPIC_CMD, "fetchTags");
        } else if (msg === "tagAddFailed") {
          setAdding(false);
          alert("Add failed or timed out on device.");
        } else if (msg.startsWith("tagDeleted:")) {
          setDeletingUid(null);
          client.publish(TOPIC_CMD, "fetchTags");
        } else if (msg === "tagDeleteFailed") {
          setDeletingUid(null);
          alert("Delete failed on device.");
        } else if (msg === "AddModeStarted") {
          console.log("Device ready for scanning");
        }
      }
    });

    client.on("error", (err) => {
      console.error("MQTT error", err);
    });

    return () => client.end();
  }, []);

  const handleAdd = () => {
    if (!clientRef.current || !clientRef.current.connected) {
      alert("MQTT not connected");
      return;
    }
    setAdding(true);
    clientRef.current.publish(TOPIC_CMD, "addTag");

    setTimeout(() => {
      if (adding) {
        setAdding(false);
        alert("Add timeout — device may not have received tag or timed out.");
      }
    }, 25000);
  };

  const handleDelete = (uid) => {
    if (!clientRef.current || !clientRef.current.connected) {
      alert("MQTT not connected");
      return;
    }
    if (!confirm(`Delete tag ${uid}?`)) return;
    setDeletingUid(uid);
    clientRef.current.publish(TOPIC_CMD, `deleteTag:${uid}`);
  };

  const handleOpenDoor = () => {
    if (!clientRef.current || !clientRef.current.connected) {
      alert("MQTT not connected");
      return;
    }
    clientRef.current.publish(TOPIC_CMD, "OPEN");
  };

  return (
    <div className="min-h-screen p-8 bg-gray-50">
      {/* Header with MQTT status */}
      <div className="flex justify-between items-center mb-6">
        <h1 className="text-2xl font-bold text-blue-800">
          Smart RFID Tag Manager
        </h1>
        <div className="flex items-center space-x-2">
          <span
            className={`h-3 w-3 rounded-full ${
              mqttConnected ? "bg-green-500" : "bg-red-500"
            }`}
          ></span>
          <span className="text-sm text-black">
            {mqttConnected ? "MQTT Connected" : "MQTT Disconnected"}
          </span>
        </div>
      </div>

      {/* Open Door Button */}
      <button
        onClick={handleOpenDoor}
        disabled={!mqttConnected}
        className={`mb-6 px-4 py-2 rounded ${
          mqttConnected
            ? "bg-green-600 hover:bg-green-700 text-white"
            : "bg-gray-400 text-gray-200 cursor-not-allowed"
        }`}
      >
        Open Door
      </button>

      {/* Tag Grid */}
      <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 gap-4">
        <AddCard onClick={handleAdd} loading={adding} />
        {tags.map((uid) => (
          <TagCard
            key={uid}
            uid={uid}
            onDelete={handleDelete}
            deleting={deletingUid === uid}
          />
        ))}
      </div>

      {/* Instructions */}
      <div className="mt-6 text-sm text-gray-600">
        <p>
          Click <span className="font-semibold">Add Tag</span>, then scan card
          on the ESP32 when the device indicates it is waiting.
        </p>
        <p>After successful add/delete the tag list refreshes automatically.</p>
      </div>
    </div>
  );
}
