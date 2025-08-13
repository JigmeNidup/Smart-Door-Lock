"use client";
import { useEffect, useState, useRef } from "react";
import mqtt from "mqtt";
import AddCard from "../components/AddCard";
import TagCard from "../components/TagCard";

export default function Home() {
  const [tags, setTags] = useState([]);
  const [adding, setAdding] = useState(false);
  const [deletingUid, setDeletingUid] = useState(null);
  const clientRef = useRef(null);

  // Update these to match your broker
  const BROKER_URL = "wss://broker.hivemq.com:8884/mqtt"; // example (secure WebSocket)
  const TOPIC_CMD = "smartlock/esp32/cmd";
  const TOPIC_EVENTS = "smartlock/esp32/events";
  const TOPIC_TAGS = "smartlock/esp32/tags";

  useEffect(() => {
    const client = mqtt.connect(BROKER_URL, { reconnectPeriod: 3000 });
    clientRef.current = client;

    client.on("connect", () => {
      console.log("MQTT connected");
      client.subscribe(TOPIC_EVENTS);
      client.subscribe(TOPIC_TAGS);
      // get initial list
      client.publish(TOPIC_CMD, "fetchTags");
    });

    client.on("message", (topic, message) => {
      const msg = message.toString();
      // console.log("msg", topic, msg);
      if (topic === TOPIC_TAGS) {
        try {
          const parsed = JSON.parse(msg);
          setTags(parsed.tags || []);
        } catch (e) {
          console.warn("Failed parse tags:", msg);
        }
      } else if (topic === TOPIC_EVENTS) {
        if (msg.startsWith("tagAdded:")) {
          const uid = msg.substring("tagAdded:".length);
          // Confirm add → fetch latest tags
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
          // optional: show a toast that device is ready for card
          console.log("Device ready for scanning");
        }
      }
    });

    client.on("error", (err) => {
      console.error("MQTT error", err);
    });

    return () => client.end();
  }, []);

  const handleAdd = async () => {
    if (!clientRef.current || !clientRef.current.connected) {
      alert("MQTT not connected");
      return;
    }
    setAdding(true);
    // send addTag command — ESP32 will enter add-mode and wait for card
    clientRef.current.publish(TOPIC_CMD, "addTag");

    // optional: safety timeout on UI to stop waiting after e.g. 25s
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
    // wait for response from device to call fetchTags
  };

  return (
    <div className="min-h-screen p-8 bg-gray-50">
      <h1 className="text-2xl font-bold mb-6 text-blue-800">Smart RFID Tag Manager</h1>

      <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 gap-4">
        <AddCard onClick={handleAdd} loading={adding} />
        {tags.map((uid) => (
          <TagCard key={uid} uid={uid} onDelete={handleDelete} deleting={deletingUid === uid} />
        ))}
      </div>

      <div className="mt-6 text-sm text-gray-600">
        <p>Click <span className="font-semibold">Add Tag</span>, then scan card on the ESP32 when the device indicates it is waiting.</p>
        <p>After successful add/delete the tag list refreshes automatically.</p>
      </div>
    </div>
  );
}
