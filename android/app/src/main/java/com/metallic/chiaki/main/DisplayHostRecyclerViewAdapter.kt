// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.main

import android.util.Log
import android.view.View
import android.view.ViewGroup
import android.view.animation.AnimationUtils
import android.widget.PopupMenu
import androidx.core.view.isGone
import androidx.core.view.isVisible
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.RecyclerView
import com.metallic.chiaki.R
import com.metallic.chiaki.common.DiscoveredDisplayHost
import com.metallic.chiaki.common.DisplayHost
import com.metallic.chiaki.common.ManualDisplayHost
import com.metallic.chiaki.common.ext.inflate
import com.metallic.chiaki.lib.DiscoveryHost
import kotlinx.android.synthetic.main.item_display_host.view.*

class DisplayHostDiffCallback(val old: List<DisplayHost>, val new: List<DisplayHost>): DiffUtil.Callback()
{
	override fun areItemsTheSame(oldItemPosition: Int, newItemPosition: Int) = (old[oldItemPosition] == new[newItemPosition])
	override fun areContentsTheSame(oldItemPosition: Int, newItemPosition: Int) = (old[oldItemPosition] == new[newItemPosition])
	override fun getOldListSize() = old.size
	override fun getNewListSize() = new.size
}

class DisplayHostRecyclerViewAdapter(
	val clickCallback: (DisplayHost) -> Unit,
	val wakeupCallback: (DisplayHost) -> Unit,
	val editCallback: (DisplayHost) -> Unit,
	val deleteCallback: (DisplayHost) -> Unit
): RecyclerView.Adapter<DisplayHostRecyclerViewAdapter.ViewHolder>()
{
	var hosts: List<DisplayHost> = listOf()
		set(value)
		{
			val diff = DiffUtil.calculateDiff(DisplayHostDiffCallback(field, value))
			field = value
			diff.dispatchUpdatesTo(this)
		}

	class ViewHolder(itemView: View): RecyclerView.ViewHolder(itemView)

	override fun onCreateViewHolder(parent: ViewGroup, viewType: Int)
			= ViewHolder(parent.inflate(R.layout.item_display_host))

	override fun getItemCount() = hosts.count()

	override fun onBindViewHolder(holder: ViewHolder, position: Int)
	{
		val context = holder.itemView.context
		val host = hosts[position]
		holder.itemView.also {
			it.nameTextView.text = host.name
			it.hostTextView.text = context.getString(R.string.display_host_host, host.host)
			val id = host.id
			it.idTextView.text =
				if(id != null)
					context.getString(
						if(host.isRegistered)
							R.string.display_host_id_registered
						else
							R.string.display_host_id_unregistered,
						id)
				else
					""
			it.bottomInfoTextView.text = (host as? DiscoveredDisplayHost)?.discoveredHost?.let { discoveredHost ->
				if(discoveredHost.runningAppName != null || discoveredHost.runningAppTitleid != null)
					context.getString(R.string.display_host_app_title_id, discoveredHost.runningAppName ?: "", discoveredHost.runningAppTitleid ?: "")
				else
					""
			} ?: ""
			it.discoveredIndicatorLayout.visibility = if(host is DiscoveredDisplayHost) View.VISIBLE else View.GONE
			it.stateIndicatorImageView.setImageResource(
				if(host is DiscoveredDisplayHost)
					when(host.discoveredHost.state)
					{
						DiscoveryHost.State.STANDBY -> R.drawable.ic_console_standby
						DiscoveryHost.State.READY -> R.drawable.ic_console_ready
						else -> R.drawable.ic_console
					}
				else
					R.drawable.ic_console)
			it.setOnClickListener { clickCallback(host) }

			val canWakeup = host.registeredHost != null
			val canEditDelete = host is ManualDisplayHost
			if(canWakeup || canEditDelete)
			{
				it.menuButton.isVisible = true
				it.menuButton.setOnClickListener { _ ->
					val menu = PopupMenu(context, it.menuButton)
					menu.menuInflater.inflate(R.menu.display_host, menu.menu)
					menu.menu.findItem(R.id.action_wakeup).isVisible = canWakeup
					menu.menu.findItem(R.id.action_edit).isVisible = canEditDelete
					menu.menu.findItem(R.id.action_delete).isVisible = canEditDelete
					menu.setOnMenuItemClickListener { menuItem ->
						when(menuItem.itemId)
						{
							R.id.action_wakeup -> wakeupCallback(host)
							R.id.action_edit -> editCallback(host)
							R.id.action_delete -> deleteCallback(host)
							else -> return@setOnMenuItemClickListener false
						}
						true
					}
					menu.show()
				}
			}
			else
			{
				it.menuButton.isGone = true
				it.menuButton.setOnClickListener(null)
			}
		}
	}
}