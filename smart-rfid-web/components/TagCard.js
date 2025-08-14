"use client";
export default function TagCard({ uid, onDelete, deleting }) {
  return (
    <div className="bg-white rounded-2xl shadow p-4 flex flex-col items-center gap-3">
      <div className="font-mono text-sm break-all text-blue-600">{uid}</div>
      <button
        onClick={() => onDelete(uid)}
        className="px-3 py-1 rounded-md bg-red-500 text-white hover:bg-red-600 disabled:opacity-50"
        disabled={deleting}
      >
        {deleting ? "Deleting..." : "Delete"}
      </button>
    </div>
  );
}
