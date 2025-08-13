"use client";

export default function AddCard({ onClick, loading }) {
  return (
    <button
      onClick={onClick}
      className={`flex flex-col items-center justify-center gap-2 rounded-2xl p-4 shadow transition ${
        loading ? "bg-yellow-50 cursor-wait" : "bg-gray-100 hover:bg-gray-200"
      }`}
      disabled={loading}
    >
      <span className="text-4xl font-bold text-gray-600">+</span>
      <span className="text-sm text-gray-600">{loading ? "Waiting..." : "Add Tag"}</span>
    </button>
  );
}
